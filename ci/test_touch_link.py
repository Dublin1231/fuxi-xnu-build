"""Negative controls for the binary audit, not substitutes for target linking."""
import struct
import unittest
from touch_link import MachO,audit_kernel

def fixture(kind=2,missing=False,bad_init=False,open_gate=False,host_start=False,unresolved=False,ctor_suffix='',debug_duplicates=False):
    names=['_fuxi_st_board_start','_st_board_init',
        '__GLOBAL__sub_I_FuxiSTWorkLoop.cpp'+ctor_suffix,'__GLOBAL__sub_I_FuxiSharedRegistry.cpp'+ctor_suffix]
    if host_start:names+=['_st_board_start_offline']
    if unresolved:names+=['_unknown_provider_write']
    strings=b'\0';symbols=b''
    for index,name in enumerate(names):
        if missing and name=='_st_board_init':continue
        symbol_kind=1 if name=='_unknown_provider_write' else 0xe if 'GLOBAL__sub_I_' in name else 0xf
        symbols+=struct.pack('<IBBHQ',len(strings),symbol_kind,1,0,0x1000+index*16)
        if debug_duplicates and name!='_unknown_provider_write':
            symbols+=struct.pack('<IBBHQ',len(strings),0x24,1,0,0x1000+index*16)
        strings+=name.encode()+b'\0'
    segment=struct.pack('<II16sQQQQiiII',0x19,232,b'__DATA_CONST',0x1000,0x200,0x200,0x120,7,3,2,0)
    segment+=struct.pack('<16s16sQQIIIIIIII',b'__text',b'__TEXT_EXEC',0x1000,0x100,0x200,2,0,0,0x80000400,0,0,0)
    segment+=struct.pack('<16s16sQQIIIIIIII',b'__mod_init_func',b'__DATA_CONST',0x1100,16,0x300,3,0,0,9,0,0,0)
    symtab=struct.pack('<6I',2,24,0x320,len(symbols)//16,0x400,len(strings))
    header=struct.pack('<8I',0xfeedfacf,0x100000c,0,kind,2,256,0,0)
    data=bytearray(0x400+len(strings));data[:288]=header+segment+symtab
    data[0x200:0x208]=struct.pack('<II',0x52800000 if open_gate else 0x128000a0,0xd65f03c0)
    data[0x300:0x310]=struct.pack('<QQ',0 if bad_init else 0x1020,0x1030)
    data[0x320:0x320+len(symbols)]=symbols;data[0x400:]=strings
    return bytes(data)

class BinaryAuditTests(unittest.TestCase):
    def audit(self,raw):return audit_kernel(raw,raw,[fixture(kind=1)],set())
    def test_accepts_retained_definitions_and_constructor_table(self):
        result=self.audit(fixture())
        self.assertTrue(result['required_definitions_retained_in_both'])
        self.assertFalse(result['device_boot_ready'])
        self.assertEqual(len(result['class_constructor_pointers']),2)
    def test_missing_definition_rejected(self):
        with self.assertRaisesRegex(ValueError,'discarded touch'):self.audit(fixture(missing=True))
    def test_constructor_omission_rejected(self):
        with self.assertRaisesRegex(ValueError,'constructor not retained'):self.audit(fixture(bad_init=True))
    def test_unique_local_constructor_suffix_retains_pointer_proof(self):
        result=self.audit(fixture(ctor_suffix='.llvm.12345'))
        self.assertEqual(len(result['class_constructor_pointers']),2)
        with self.assertRaisesRegex(ValueError,'constructor not retained'):
            self.audit(fixture(ctor_suffix='.llvm.12345',bad_init=True))
    def test_debug_records_do_not_duplicate_live_initializers_or_gate(self):
        result=self.audit(fixture(debug_duplicates=True))
        self.assertEqual(result['production_gate'],'constant ST_BLOCKED; no calls')
        self.assertEqual(len(result['class_constructor_pointers']),2)
        for kw in [dict(bad_init=True),dict(open_gate=True)]:
            with self.subTest(kw=kw),self.assertRaises(ValueError):self.audit(fixture(debug_duplicates=True,**kw))
    def test_enabled_gate_and_host_start_rejected(self):
        for kw in [dict(open_gate=True),dict(host_start=True)]:
            with self.subTest(kw=kw),self.assertRaises(ValueError):self.audit(fixture(**kw))
    def test_new_undefined_provider_rejected(self):
        with self.assertRaisesRegex(ValueError,'unresolved imports'):self.audit(fixture(unresolved=True))
    def test_relocatable_is_not_kernel(self):
        with self.assertRaisesRegex(ValueError,'executable kernel'):self.audit(fixture(kind=1))
    def test_malformed_header_and_bounds_rejected(self):
        for raw in [b'',fixture()[:36],fixture()[:0x405]]:
            with self.subTest(length=len(raw)),self.assertRaises(ValueError):MachO(raw)

if __name__=='__main__':unittest.main()
