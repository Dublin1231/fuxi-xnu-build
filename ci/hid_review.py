"""Run the pinned original-image HID ABI audit; no guessed ABI or device calls."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

def descriptor_contract(text):
    finger=re.search(r'#define FINGER_DESC\s*\\\n(.*?)static const unsigned char touchDescriptor',text,re.S)
    body=re.search(r'touchDescriptor\[\]=\{(.*?)\};',text,re.S)
    if not finger or not body:raise ValueError('Pinned digitizer descriptor absent')
    expanded=body[1].replace('FINGER_DESC',finger[1])
    values=[int(v,16) for v in re.findall(r'0x[0-9a-fA-F]+',expanded)]
    size=count=report=0;inputs={};i=0
    while i<len(values):
        prefix=values[i];i+=1
        if prefix==0xfe:raise ValueError('Unexpected HID long item')
        length=[0,1,2,4][prefix&3]
        if i+length>len(values):raise ValueError('Truncated HID descriptor')
        value=sum(values[i+j]<<(8*j) for j in range(length));i+=length
        if prefix==0x75:size=value
        elif prefix==0x95:count=value
        elif prefix==0x85:report=value
        elif prefix&0xfc==0x80:inputs[report]=inputs.get(report,0)+size*count
    if inputs!={3:488}:raise ValueError('Digitizer no longer matches ten contacts / 62-byte Report 3')
    if values[:8]!=[5,13,9,4,161,1,133,3]:raise ValueError('Unexpected touch collection')
    return dict(report_id=3,payload_bits=488,bytes_including_report_id=62,contacts=10)

def qualify(tree,native,candidate,run,out):
    source=tree/'drivers/NativeHidInput/NativeHidInput.cpp'
    expected='0445e5f451ffcc827bdd603e822180444d998427fa3a6d63559b1a7b8f850998'
    if hashlib.sha256(source.read_bytes()).hexdigest()!=expected:
        raise ValueError('NativeHidInput source does not match pinned ba03e5d revision')
    original=source.read_text()
    descriptor=descriptor_contract(original)
    patch=native/'NativeHidInput.fuxi.patch'
    run(['git','apply','--check',patch],cwd=source.parent)
    run(['git','apply',patch],cwd=source.parent)
    for name in ['NativeFuxiHid.h','fuxi_hid_reports.h','fuxi_st_bridge.h','fuxi_st.h']:
        target=tree/'build/native-hid-input'/name;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes((native/name).read_bytes())
    upstream=tree/'scripts/build-native-hid-input.py';text=upstream.read_text()
    start=text.index("p=ROOT/'build/native-prelink/")
    end=text.index('b=p.read_bytes();',start)
    # Use the actual freshly emitted candidate plus its hash-checked plan.
    # Retain every original instruction, vtable, instance-size and binding check.
    revised=text[:start]+"p=Path(sys.argv[1])\n"+text[end:]
    audited=tree/'scripts/ci-qualify-fuxi-hid.py';audited.write_text(revised)
    report=dict(descriptor=descriptor,private_binary_abi_audited=False,
        runtime_qualified=False,hid_linked_into_kernel=False,hardware_tested=False,
        upstream_audit_sha256=hashlib.sha256(upstream.read_bytes()).hexdigest(),
        audit_overlay_sha256=hashlib.sha256(audited.read_bytes()).hexdigest(),
        patch_sha256=hashlib.sha256(patch.read_bytes()).hexdigest(),
        original_source_sha256=hashlib.sha256(original.encode()).hexdigest(),
        patched_source_sha256=hashlib.sha256(source.read_bytes()).hexdigest())
    rc=run([sys.executable,audited,candidate],cwd=tree,check=False)
    report['exit_code']=rc
    audit=tree/'build/native-hid-input/abi-audit.json'
    if rc==0:
        data=json.loads(audit.read_text());report['private_binary_abi_audited']=True
        report['original_abi']=data
        (out/'hid-original-abi.json').write_text(json.dumps(data,indent=2)+'\n')
    (out/'hid-review.json').write_text(json.dumps(report,indent=2)+'\n')
    print('HID_REVIEW '+json.dumps(report),flush=True)
    if rc:raise RuntimeError('Pinned HID ABI audit failed; no substitute interface allowed')
    return report
