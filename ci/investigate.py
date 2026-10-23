"""Real macOS investigation, no fake SDK, no device commands or binary upload."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import tomllib
from touch_link import MachO,audit_kernel

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
    # clang -MJ normalizes -DKERNEL into two arguments: -D, KERNEL.
    definitions=set()
    for index,arg in enumerate(args):
        value=args[index+1] if arg=='-D' and index+1<len(args) else arg[2:] if arg.startswith('-D') else ''
        if value:definitions.add(value.split('=',1)[0])
    if 'KERNEL' not in definitions:raise ValueError('Receipt lacks kernel compilation context')
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
    # Upstream may rewrite branch history. Fetch the audited object explicitly;
    # never silently advance the pin to make checkout succeed.
    run(['git','fetch','--no-tags','origin',pin],cwd=directory)
    run(['git','checkout','--detach',pin],cwd=directory)
    actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=directory,text=True).strip()
    if actual!=pin:raise ValueError('Commit pin mismatch')
    return directory

CI_ARCHIVES=[
    'build-corecrypto-14.py',
    'build-corecrypto-14.py --namespace',
    'build-arm-driver.py ArmDTPlatform',
    'build-arm-driver.py ArmCpu',
    'build-arm-driver.py NativeAES',
    'build-native-aes-bindings.py',
    'build-libpthread-kernel.py',
]
EXCLUDED_ARCHIVES=['audit-storage-interface.py','build-arm-driver.py NativeUfs',
    'build-boot-progress.py','build-legacy-stub.py']

def prepare_ci_inputs(core,device):
    """Build enabled source dependencies; omit disabled storage/display components."""
    original=device/'configs/fuxi_defconfig'
    config=original.read_text()
    if config.count('CONFIG_XNU_BOOT_PROGRESS=y')!=1:raise ValueError('Unexpected base config')
    for feature in ['SOURCE_CORECRYPTO14','SOURCE_PTHREAD','ARM_DT_PLATFORM','ARMCPU','NATIVE_AES']:
        if config.splitlines().count('CONFIG_XNU_'+feature+'=y')!=1:
            raise ValueError('Archive plan requires enabled '+feature)
    if config.splitlines().count('CONFIG_XNU_NATIVE_UFS=n')!=1:raise ValueError('CI requires disabled UFS')
    if re.search(r'^CONFIG_XNU_BUILD_ARCHIVES=',config,re.M):raise ValueError('Unexpected archive gate')
    legacy='CONFIG_XNU_EXTRA_LD_ARCHIVES="{tree}/build/legacy-stub/libLegacyStub.a"'
    if config.splitlines().count(legacy)!=1:raise ValueError('Unexpected extra archives')
    if re.search(r'^CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y$',config,re.M):raise ValueError('Legacy display enabled')
    board=device/'board.toml';board_text=board.read_text()
    entries=tomllib.loads(board_text)['kernel']['archives']
    if len(entries)!=len(set(entries)) or set(entries)!=set(CI_ARCHIVES+EXCLUDED_ARCHIVES):
        raise ValueError('Unexpected upstream archive plan')
    if [e for e in entries if e in CI_ARCHIVES]!=CI_ARCHIVES:raise ValueError('Unexpected archive order')
    board_overlay,count=re.subn(r'^archives = \[\n.*?^\]',
        'archives = [\n'+''.join('  '+json.dumps(e)+',\n' for e in CI_ARCHIVES)+']',board_text,flags=re.M|re.S)
    if count!=1 or tomllib.loads(board_overlay)['kernel']['archives']!=CI_ARCHIVES:
        raise ValueError('Archive overlay did not match')
    config=config.replace('CONFIG_XNU_BOOT_PROGRESS=y','CONFIG_XNU_BOOT_PROGRESS=n')
    config=config.replace(legacy,'CONFIG_XNU_EXTRA_LD_ARCHIVES=""')
    config+='\nCONFIG_XNU_BUILD_ARCHIVES=y\n'
    target=device/'configs/fuxi_ci_defconfig';target.write_text(config)
    patch=core/'patches/xnu/offline-component-load-probes.patch'
    text=patch.read_text();before=hashlib.sha256(patch.read_bytes()).hexdigest()
    start='+\tint legacy_register = 0;\n'
    end='+\tint client_probe = 0;\n'
    if text.count(start)!=1 or text.count(end)!=1:raise ValueError('Unexpected upstream display patch')
    text=text.replace(start,'+#if defined(XNU_NATIVE_LEGACY_DISPLAY) && XNU_NATIVE_LEGACY_DISPLAY\n'+start)
    text=text.replace(end,'+#endif\n'+end)
    # Recount modified unified diff hunks while retaining their base context.
    lines=text.splitlines(keepends=True)
    for index,line in enumerate(lines):
        match=re.match(r'@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)',line)
        if not match:continue
        old=new=0
        for following in lines[index+1:]:
            if following.startswith(('@@','--- a/','+++ b/','diff --git')):break
            if following.startswith(' '):old+=1;new+=1
            elif following.startswith('-'):old+=1
            elif following.startswith('+'):new+=1
            elif following.startswith('\\'):continue
            else:break
        lines[index]=f'@@ -{match[1]},{old} +{match[2]},{new} @@{match[3]}\n'
    patch.write_text(''.join(lines))
    board.write_text(board_overlay)
    changes=dict(configuration='fuxi_ci_defconfig',boot_progress=False,
        reason='Console-only CI lacks audited UFS/owner-root dashboard dependencies',
        display_patch_before=before,display_patch_after=hashlib.sha256(patch.read_bytes()).hexdigest(),
        display_call='Guarded by existing XNU_NATIVE_LEGACY_DISPLAY flag; no stub supplied',
        build_archives=True,archive_builders=CI_ARCHIVES,excluded_disabled_components=EXCLUDED_ARCHIVES,
        board_before=hashlib.sha256(board_text.encode()).hexdigest(),
        board_after=hashlib.sha256(board_overlay.encode()).hexdigest(),
        extra_link_archives=[],crypto_replaced_with_stub=False,
        hardware_features_added=False)
    return changes

def kernel():
    core=checkout('core');device=checkout('device')
    import tomllib
    feed=tomllib.loads((core/'feeds.toml').read_text())
    if next(s['commit'] for s in feed['source'] if s['name']=='xnu')!=XNU:raise ValueError('XNU pin mismatch')
    save('ci-input-changes.json',prepare_ci_inputs(core,device))
    run([sys.executable,core/'unixnu','defconfig','fuxi_ci_defconfig','-D',device],cwd=core)
    baseline=run([sys.executable,core/'unixnu','build','kernel','-D',device,'-j','2'],cwd=core,check=False)
    tree=core/'out/fuxi/tree'
    receipt=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
    report=dict(baseline_kernel_exit_code=baseline,compile_receipt_present=receipt.is_file(),
        cpp_objects=[],c_objects=[],iokit_compile_passed=False,touch_kernel_linked=False,
        physical_provider_implemented=False,hardware_tested=False,device_boot_ready=False)
    save('kernel.json',report)
    if not receipt.is_file():raise RuntimeError('No actual IOKit compile receipt; see baseline diagnostics')
    record=json.loads(receipt.read_text().strip().rstrip(','))
    save('target-cpp-receipt.json',record)
    print('TARGET_CPP_RECEIPT '+json.dumps(record),flush=True)
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
    c_receipt=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/prng_random.o.json'
    c_record=json.loads(c_receipt.read_text().strip().rstrip(','))
    if not c_record['file'].endswith('.c'):raise ValueError('Expected genuine C receipt')
    save('target-c-receipt.json',c_record)
    print('TARGET_C_RECEIPT '+json.dumps(c_record),flush=True)
    sources=[NATIVE/p for p in ['fuxi_st.c','fuxi_st_bridge.c','fuxi_st_geni.c','board_lifecycle.c',
        'shared/shared_resources.c','shared/shared_board_backend.c','provider/cmddb.c','provider/vrm_voltage.c']]
    sources.append(tree/'drivers/NativeGeniSpi/GeniSpi.c')
    objects=[OUT/'FuxiSTWorkLoop.o',OUT/'FuxiSharedRegistry.o']
    for source in sources:
        obj=OUT/(source.stem+'.o')
        command=rewrite_cpp(c_record,source,obj)+['-I',str(tree/'drivers/NativeGeniSpi')]
        save(source.stem+'-command.json',command)
        rc=run(command,cwd=c_record['directory'],check=False)
        entry=dict(source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),exit_code=rc)
        if rc==0:entry.update(object_info(obj));objects.append(obj)
        report['c_objects'].append(entry);save('kernel.json',report)
    report['all_touch_objects_compiled']=all(o['exit_code']==0 for o in report['c_objects'])
    save('kernel.json',report)
    if not report['all_touch_objects_compiled']:raise RuntimeError('Touch C target compilation failed')
    archive=OUT/'libFuxiTouch.a'
    run(['xcrun','ar','rcs',archive,*objects])
    raw_kernel=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf'
    before=MachO(raw_kernel.read_bytes())
    baseline_undefined=before.undefined()
    definitions=set().union(*(MachO(p.read_bytes()).defined() for p in objects))
    imports=set().union(*(MachO(p.read_bytes()).undefined() for p in objects))-definitions
    unavailable=imports-before.defined()-{'___dso_handle'}
    save('touch-imports.json',dict(imports=sorted(imports),unavailable_before_link=sorted(unavailable)))
    if unavailable:raise RuntimeError('Missing real kernel imports: '+str(sorted(unavailable)))
    config=core/'out/fuxi/.config';config_text=config.read_text()
    empty='CONFIG_XNU_EXTRA_LD_ARCHIVES=""'
    if config_text.splitlines().count(empty)!=1:raise ValueError('Unexpected active extra archive inputs')
    if any(c.isspace() for c in str(archive)) or ',' in str(archive):raise ValueError('Unsafe linker path')
    config.write_text(config_text.replace(empty,'CONFIG_XNU_EXTRA_LD_ARCHIVES="-Wl,-force_load,'+str(archive)+'"'))
    # This is our disposable runner build output. Force a new executable link;
    # do not mistake a previously successful baseline for touch integration.
    raw_kernel.unlink()
    link_rc=run([sys.executable,core/'unixnu','build','kernel','-D',device,'-j','2'],cwd=core,check=False)
    report['touch_link_exit_code']=link_rc;save('kernel.json',report)
    if link_rc:raise RuntimeError('Touch kernel executable link/prelink failed')
    link_inputs=raw_kernel.parent/'mach.development.bcm2837.link/mach.development.bcm2837.linkarchives'
    if str(archive) not in link_inputs.read_text():raise RuntimeError('Actual linker inputs omit touch archive')
    audit=audit_kernel(raw_kernel.read_bytes(),(core/'out/fuxi/kernel/kernel.macho').read_bytes(),
        [p.read_bytes() for p in objects],baseline_undefined)
    audit['archive_sha256']=hashlib.sha256(archive.read_bytes()).hexdigest()
    audit['actual_link_inputs']=link_inputs.read_text()
    save('touch-link-audit.json',audit)
    print('TOUCH_LINK_AUDIT '+json.dumps(audit),flush=True)
    report['touch_kernel_linked']=True
    report['initialization_path']='Two metaclass constructors retained in kernel table; runtime not observed; no IOService personality'
    report['hid_device_abi_qualified']=False
    save('kernel.json',report)
    print('Full native touch dependency objects linked into executable kernel and prelink; hardware startup remains blocked.')

def summary():
    pieces=[]
    for name in ['host.json','kernel.json']:
        if (OUT/name).exists():pieces.append((name,json.loads((OUT/name).read_text())))
    text='# fuxi macOS investigation\n\n'
    for name,data in pieces:text+='## '+name+'\n\n```json\n'+json.dumps(data,indent=2)+'\n```\n\n'
    text+='Physical provider, HID device ABI qualification, service matching and hardware startup remain incomplete. No device operation or boot image.\n'
    print(text)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as f:f.write(text)

if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    parser=argparse.ArgumentParser();parser.add_argument('phase',choices=['host','kernel','summary']);args=parser.parse_args()
    {'host':host,'kernel':kernel,'summary':summary}[args.phase]()
