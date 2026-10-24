"""Link qualified HID plus its deferred registration and board owner, never boot."""
import hashlib
import json
import struct
from touch_link import MachO,audit_kernel

def audit_service(kernel,carrier,objects,baseline):
    result=audit_kernel(kernel,carrier,objects,baseline,
        ('FuxiSTWorkLoop.cpp','FuxiSharedRegistry.cpp','FuxiSTService.cpp'))
    for data in (kernel,carrier):
        image=MachO(data)
        constructors=[s for s in image.symbols if s['name']=='__GLOBAL__sub_I_NativeHidInput.cpp'
            and not s['kind']&0xe0 and s['kind']&0xe==14]
        if len(constructors)!=1 or constructors[0]['value'] in image.constructor_targets():
            raise ValueError('HID constructor missing or runs before original family is loaded')
        policy=[s for s in image.symbols if s['name']=='_native_fuxi_st_boot_policy'
            and not s['kind']&0xe0 and s['kind']&0xe==14]
        if len(policy)!=1 or image.code_at(policy[0]['value'],8)!=struct.pack('<II',0x128000a0,0xd65f03c0):
            raise ValueError('Board service admission gate is no longer constant ST_BLOCKED')
    return dict(result,hid_object_linked=True,hid_constructor_deferred=True,
        board_service_compiled_and_linked=True,service_boot_policy='constant ST_BLOCKED; no calls',
        concrete_provider_present=False,personality_installed=False,runtime_qualified=False)

def integrate(core,device,tree,native,out,record,base_objects,baseline,run,rewrite,original_abi):
    objects=list(base_objects)
    for name in ['FuxiSTService','FuxiHidRegistration']:
        target=out/(name+'.o')
        run(rewrite(record,native/(name+'.cpp'),target)+['-Werror=shadow-field',
            '-Werror=missing-prototypes','-Werror=switch-default'],cwd=record['directory'])
        objects.append(target)
    hid=tree/'build/native-hid-input/NativeHidInput.late.o'
    bindings=tree/'build/native-hid-input/hid-original-bindings.o'
    parsed=MachO(hid.read_bytes())
    if parsed.constructor_targets():raise ValueError('Deferred HID object still has constructor pointers')
    forbidden=('native_touch_','native_buttons_','native_hid_input_','native_hid_live_')
    if any(any(term in name for term in forbidden) for name in parsed.undefined()):
        raise ValueError('Legacy board/probe import leaked into fuxi HID')
    objects.extend([hid,bindings])
    archive=out/'libFuxiTouch.a'
    run(['xcrun','libtool','-static','-o',archive]+objects)
    raw=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf'
    raw.unlink()
    run(['python3',core/'unixnu','build','kernel','-D',device,'-j','2'],cwd=core)
    candidate=core/'out/fuxi/kernel/kernel.macho'
    result=audit_service(raw.read_bytes(),candidate.read_bytes(),[p.read_bytes() for p in objects],baseline)
    plan=json.loads(candidate.with_suffix('.json').read_text())
    if plan['candidate_sha256']!=hashlib.sha256(candidate.read_bytes()).hexdigest():
        raise ValueError('Final carrier plan does not match its bytes')
    if plan['original_executable_sha256']['com.apple.iokit.IOHIDFamily']!=original_abi['original_text_sha256']:
        raise ValueError('Original HID instructions changed after service integration')
    symbols={s['name']:s['value'] for s in MachO(raw.read_bytes()).symbols if s['kind']&0xe in (2,14) and not s['kind']&0xe0}
    if any(symbols.get(name)!=address for name,address in original_abi['bindings'].items()):
        raise ValueError('Final kernel bindings differ from the qualified original ABI')
    result['original_hid_identity_and_bindings_rechecked']=True
    result['archive_sha256']=hashlib.sha256(archive.read_bytes()).hexdigest()
    (out/'hid-service.json').write_text(json.dumps(result,indent=2)+'\n')
    print('HID_SERVICE '+json.dumps(result),flush=True)
    return result
