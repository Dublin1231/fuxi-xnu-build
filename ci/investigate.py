"""Real macOS investigation, no fake SDK, no device commands or binary upload."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/cloud'
NATIVE=ROOT/'adaptation/fuxi/native'
PINS={'core':('https://github.com/UniXNU/UniXNU.git','245aca1a9ec5b730b1cad80fade295800d2981f1'),
      'device':('https://github.com/hanzi123-bilp/unixnu-fuxi.git','d12c0585c7c82941c17e4af6bace12dc46953c85')}
XNU='d8b80295118ef25ac3a784134bcf95cd8e88109f'

def save(name,value):
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/name).write_text(json.dumps(value,indent=2)+'\n')

def run(command,cwd=ROOT,check=True):
    command=[str(c) for c in command]
    print('RUN '+json.dumps(command),flush=True)
    # Stream real output to Actions logs. Never print process environment/tokens.
    result=subprocess.run(command,cwd=cwd)
    with (OUT/'commands.jsonl').open('a') as log:
        log.write(json.dumps(dict(command=command,cwd=str(cwd),exit_code=result.returncode))+'\n')
    if check and result.returncode:raise RuntimeError('Command failed: '+str(command[0]))
    return result.returncode

def object_info(path):
    data=path.read_bytes()
    magic,cpu,_,kind,ncmd,_,_,_=struct.unpack_from('<8I',data)
    if (magic,cpu,kind)!=(0xfeedfacf,0x100000c,1):raise ValueError('Expected ARM64 Mach-O object')
    offset=32;platforms=[]
    for _ in range(ncmd):
        cmd,size=struct.unpack_from('<II',data,offset)
        if size<8 or offset+size>len(data):raise ValueError('Invalid load command')
        if cmd==0x32:platforms.append(struct.unpack_from('<I',data,offset+8)[0])
        offset+=size
    if 2 not in platforms:raise ValueError('Object does not declare iOS platform')
    return dict(sha256=hashlib.sha256(data).hexdigest(),format='ARM64 iOS Mach-O relocatable')

def rewrite_cpp(record,source,destination):
    args=record['arguments']
    if not isinstance(args,list) or not args or not all(isinstance(a,str) for a in args):raise ValueError('Bad compiler receipt')
    if Path(args[0]).name not in ('clang','clang++'):raise ValueError('Receipt is not a direct clang invocation')
    if args.count(record['file'])!=1 or args.count('-o')!=1 or '-c' not in args:raise ValueError('Ambiguous compile receipt')
    if not any(a.startswith('-DKERNEL') for a in args):raise ValueError('Receipt lacks kernel compilation context')
    command=[];i=0
    paired={'-o','-MF','-MT','-MQ','-MJ','--serialize-diagnostics','-serialize-diagnostics'}
    while i<len(args):
        arg=args[i]
        if arg in paired:
            if i+1>=len(args):raise ValueError('Truncated output option')
            if arg=='-o':command+=['-o',str(destination)]
            i+=2;continue
        # Dependency and diagnostics outputs must not overwrite upstream records.
        if any(arg.startswith(p) and arg!=p for p in ['-o','-MF','-MT','-MQ','-MJ']):
            raise ValueError('Unsupported attached output option; inspect real recipe')
        if arg in ('-MD','-MMD','-MP','-MG'):i+=1;continue
        command.append(str(source) if arg==record['file'] else arg);i+=1
    # Preserve target ABI, language and warning flags from actual XNU recipe.
    return command

def host():
    if platform.system()!='Darwin':raise RuntimeError('Requires real macOS host')
    run([sys.executable,'-m','unittest','discover','-s','ci','-p','test_*.py','-v'])
    for command in [['sw_vers'],['uname','-m'],['xcodebuild','-version'],
                    ['xcrun','--sdk','iphoneos','--show-sdk-version'],['xcrun','--sdk','iphoneos','--find','mig']]:run(command)
    cc=['xcrun','clang','-std=c11','-Wall','-Wextra','-Werror','-O2',
        '-fsanitize=undefined','-fno-sanitize-recover=undefined']
    core=[NATIVE/'board_lifecycle.c',NATIVE/'fuxi_st.c',NATIVE/'fuxi_st_bridge.c']
    suites=[('lifecycle',['-DST_BOARD_HOST_TEST',NATIVE/'test_board_lifecycle.c']+core),
        ('shared',['-DST_BOARD_HOST_TEST',NATIVE/'shared/test_shared_resources.c',NATIVE/'shared/shared_resources.c',
            NATIVE/'shared/shared_board_backend.c']+core),
        ('provider',[NATIVE/'provider/test_cmddb.c',NATIVE/'provider/cmddb.c',NATIVE/'provider/vrm_voltage.c'])]
    for name,sources in suites:
        run(cc+sources+['-o',OUT/name]);run([OUT/name])
    save('host.json',dict(status='HOST_TESTS_PASSED',iokit_compile_passed=False,touch_kernel_linked=False,hardware_tested=False))

def checkout(name):
    url,pin=PINS[name];directory=ROOT/'upstream'/name
    directory.parent.mkdir(parents=True,exist_ok=True)
    run(['git','clone','--no-checkout',url,directory])
    run(['git','checkout','--detach',pin],cwd=directory)
    actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=directory,text=True).strip()
    if actual!=pin:raise ValueError('Commit pin mismatch')
    return directory

def kernel():
    core=checkout('core');device=checkout('device')
    import tomllib
    feed=tomllib.loads((core/'feeds.toml').read_text())
    if next(s['commit'] for s in feed['source'] if s['name']=='xnu')!=XNU:raise ValueError('XNU pin mismatch')
    run([sys.executable,core/'unixnu','defconfig','fuxi_defconfig','-D',device],cwd=core)
    baseline=run([sys.executable,core/'unixnu','build','kernel','-D',device,'-j','2'],cwd=core,check=False)
    tree=core/'out/fuxi/tree'
    receipt=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
    report=dict(baseline_kernel_exit_code=baseline,compile_receipt_present=receipt.is_file(),
        cpp_objects=[],iokit_compile_passed=False,touch_kernel_linked=False,
        physical_provider_implemented=False,hardware_tested=False,device_boot_ready=False)
    save('kernel.json',report)
    if not receipt.is_file():raise RuntimeError('No actual IOKit compile receipt; see baseline diagnostics')
    record=json.loads(receipt.read_text().strip().rstrip(','))
    # Use original working directory and generated include paths, not a fabricated SDK.
    if not Path(record['directory']).is_dir():raise RuntimeError('Receipt working directory absent')
    for source in [NATIVE/'FuxiSTWorkLoop.cpp',NATIVE/'shared/FuxiSharedRegistry.cpp']:
        obj=OUT/(source.stem+'.o')
        command=rewrite_cpp(record,source,obj)
        save(source.stem+'-command.json',command)
        rc=run(command,cwd=record['directory'],check=False)
        entry=dict(source=str(source.relative_to(ROOT)),exit_code=rc)
        if rc==0:entry.update(object_info(obj))
        report['cpp_objects'].append(entry);save('kernel.json',report)
    report['iokit_compile_passed']=all(o['exit_code']==0 for o in report['cpp_objects'])
    save('kernel.json',report)
    if baseline or not report['iokit_compile_passed']:raise RuntimeError('Build/compile investigation has failures')
    print('Baseline and C++ objects passed; touch objects NOT linked into kernel. Hardware still unavailable.')

def summary():
    pieces=[]
    for name in ['host.json','kernel.json']:
        if (OUT/name).exists():pieces.append((name,json.loads((OUT/name).read_text())))
    text='# fuxi macOS investigation\n\n'
    for name,data in pieces:text+='## '+name+'\n\n```json\n'+json.dumps(data,indent=2)+'\n```\n\n'
    text+='Physical provider and touch kernel integration are incomplete. No device operation or boot image.\n'
    print(text)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as f:f.write(text)

if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    parser=argparse.ArgumentParser();parser.add_argument('phase',choices=['host','kernel','summary']);args=parser.parse_args()
    {'host':host,'kernel':kernel,'summary':summary}[args.phase]()
