"""Inspect actual Mach-O symbols and constructor pointers; never execute a kernel."""
import hashlib
import struct

class MachO:
    def __init__(self,data):
        self.data=data
        magic,cpu,_,self.kind,count,commands_size,_,_=self.unpack('<8I',0)
        if (magic,cpu)!=(0xfeedfacf,0x100000c):raise ValueError('Expected ARM64 Mach-O')
        if commands_size>len(data)-32:raise ValueError('Truncated commands')
        self.sections=[];symtab=None;offset=32
        for _ in range(count):
            command,size=self.unpack('<II',offset)
            if size<8 or offset+size>32+commands_size:raise ValueError('Invalid command size')
            if command==0x19:
                if size<72:raise ValueError('Truncated segment')
                nsects=self.unpack('<I',offset+64)[0]
                if 72+80*nsects>size:raise ValueError('Truncated sections')
                for i in range(nsects):
                    at=offset+72+i*80
                    name=data[at:at+16].split(b'\0')[0].decode()
                    segment=data[at+16:at+32].split(b'\0')[0].decode()
                    address,length,fileoff,_,reloff,nreloc,flags=self.unpack('<QQIIIII',at+32)
                    self.sections.append(dict(name=name,segment=segment,address=address,size=length,
                        offset=fileoff,flags=flags,reloff=reloff,nreloc=nreloc))
            if command==2:
                if size!=24:raise ValueError('Bad symtab command')
                symtab=self.unpack('<4I',offset+8)
            offset+=size
        if offset!=32+commands_size or symtab is None:raise ValueError('Missing/inconsistent symbol table')
        symoff,nsyms,stroff,strsize=symtab
        self.slice(symoff,nsyms*16);strings=self.slice(stroff,strsize)
        self.symbols=[]
        for i in range(nsyms):
            index,kind,section,desc,value=self.unpack('<IBBHQ',symoff+i*16)
            if index>=strsize:raise ValueError('Invalid string index')
            end=strings.find(b'\0',index)
            if end<0:raise ValueError('Unterminated symbol')
            self.symbols.append(dict(name=strings[index:end].decode(),kind=kind,section=section,value=value,desc=desc))
    def slice(self,offset,size):
        if offset<0 or size<0 or offset+size>len(self.data):raise ValueError('Truncated Mach-O data')
        return self.data[offset:offset+size]
    def unpack(self,fmt,offset):
        return struct.unpack(fmt,self.slice(offset,struct.calcsize(fmt)))
    def defined(self):
        return {s['name'] for s in self.symbols if not s['kind']&0xe0 and s['kind']&0xe in (2,14)}
    def undefined(self):
        return {s['name'] for s in self.symbols if not s['kind']&0xe0 and s['kind']&0xe==0 and s['kind']&1}
    def constructor_targets(self):
        result=[]
        for s in self.sections:
            if s['flags']&0xff==9:
                if s['size']%8:raise ValueError('Invalid constructor section')
                result.extend(self.unpack('<Q',s['offset']+i)[0] for i in range(0,s['size'],8))
        return result
    def code_at(self,address,length):
        for s in self.sections:
            if s['address']<=address and address+length<=s['address']+s['size']:
                return self.slice(s['offset']+address-s['address'],length)
        raise ValueError('Symbol outside file-backed sections')

    def initializer_diagnostics(self):
        return dict(constructor_sections=[dict(s,targets=[hex(v) for v in
            [self.unpack('<Q',s['offset']+i)[0] for i in range(0,s['size'],8)]])
            for s in self.sections if s['flags']&0xff==9],
            touch_symbols=[dict(s,value=hex(s['value'])) for s in self.symbols if 'Fuxi' in s['name']],
            gate_symbols=[dict(s,value=hex(s['value']),code=self.code_at(s['value'],8).hex())
                for s in self.symbols if s['name']=='_fuxi_st_board_start' and not s['kind']&0xe0
                and s['kind']&0xe==14])

def audit_kernel(kernel,prelinked,objects,baseline_undefined,constructor_files=('FuxiSTWorkLoop.cpp','FuxiSharedRegistry.cpp')):
    image=MachO(kernel);carrier=MachO(prelinked)
    if image.kind!=2 or carrier.kind!=2:raise ValueError('Expected executable kernel images, not relocatable link')
    required=set()
    for raw in objects:
        obj=MachO(raw)
        if obj.kind!=1:raise ValueError('Expected actual relocatable inputs')
        required|={s['name'] for s in obj.symbols if not s['kind']&0xe0 and s['kind']&1 and s['kind']&0xe in (2,14)}
    for label,parsed in [('kernel',image),('prelinked',carrier)]:
        missing=required-parsed.defined()
        if missing:raise ValueError(label+' discarded touch definitions: '+str(sorted(missing)))
        new=parsed.undefined()-set(baseline_undefined)
        if new:raise ValueError(label+' introduced unresolved imports: '+str(sorted(new)))
    expected=constructor_files;initializers=[]
    targets=image.constructor_targets()
    for filename in expected:
        # LLVM can append a uniqueness suffix to local initializer names.
        # The actual pointer-table membership, not a filename suffix, is proof.
        matches=[s for s in image.symbols if 'GLOBAL__sub_I_' in s['name'] and filename in s['name']
            and not s['kind']&0xe0 and s['kind']&0xe==14 and s['value'] in targets]
        if len(matches)!=1:
            raise ValueError('Class constructor not retained in kernel initializer table: '+filename)
        initializers.append(dict(symbol=matches[0]['name'],address=hex(matches[0]['value'])))
    gates=[s for s in image.symbols if s['name']=='_fuxi_st_board_start' and not s['kind']&0xe0
        and s['kind']&0xe==14]
    # Ordinary ARM64 -O2: mov w0, #-6 (ST_BLOCKED); ret. Reject any changed/calling gate.
    if len(gates)!=1 or image.code_at(gates[0]['value'],8)!=struct.pack('<II',0x128000a0,0xd65f03c0):
        raise ValueError('Production gate is not the qualified constant ST_BLOCKED return')
    if '_st_board_start_offline' in image.defined():raise ValueError('Host-only start leaked into kernel')
    return dict(kernel_sha256=hashlib.sha256(kernel).hexdigest(),
        prelinked_sha256=hashlib.sha256(prelinked).hexdigest(),required_definitions=sorted(required),
        required_definitions_retained_in_both=True,new_unresolved_symbols=[],
        class_constructor_pointers=initializers,production_gate='constant ST_BLOCKED; no calls',
        host_only_start_absent=True,runtime_registration_observed=False,
        io_service_personality_present=False,hid_device_abi_qualified=False,
        physical_provider_implemented=False,hardware_tested=False,device_boot_ready=False)
