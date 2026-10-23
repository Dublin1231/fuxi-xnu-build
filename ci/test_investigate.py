"""Compiler receipt rewrite safeguards; these do not emulate an SDK build."""
from pathlib import PurePosixPath as Path
import unittest
from investigate import rewrite_cpp
from investigate import prepare_ci_inputs
from pathlib import Path as HostPath
from tempfile import TemporaryDirectory

class ReceiptTests(unittest.TestCase):
    def receipt(self):
        return dict(file='original.cpp',arguments=['/usr/bin/clang','-target','arm64-apple-ios17.0',
            '-DKERNEL=1','-fapple-kext','-fno-rtti','-I','generated','-Werror','-c','original.cpp',
            '-o','original.cpo','-MD','-MF','original.d','-MT','original.cpo','-MJ','original.cpo.json'])
    def test_preserve_abi_and_redirect_output(self):
        r=self.receipt();cmd=rewrite_cpp(r,Path('/new/source.cpp'),Path('/new/object.o'))
        self.assertEqual(cmd,['/usr/bin/clang','-target','arm64-apple-ios17.0','-DKERNEL=1',
            '-fapple-kext','-fno-rtti','-I','generated','-Werror','-c','/new/source.cpp','-o','/new/object.o'])
        self.assertFalse(any('original.' in v for v in cmd))
    def test_source_must_be_unique(self):
        r=self.receipt();r['arguments']+=['original.cpp']
        with self.assertRaises(ValueError):rewrite_cpp(r,Path('new.cpp'),Path('new.o'))
    def test_split_clang_definitions_preserved(self):
        r=self.receipt();at=r['arguments'].index('-DKERNEL=1');r['arguments'][at:at+1]=['-D','KERNEL','-D','KERNEL_PRIVATE']
        cmd=rewrite_cpp(r,Path('new.cpp'),Path('new.o'))
        self.assertEqual(cmd[at:at+4],['-D','KERNEL','-D','KERNEL_PRIVATE'])
    def test_similar_macro_does_not_qualify(self):
        r=self.receipt();r['arguments'][3]='-DKERNEL_PRIVATE'
        with self.assertRaises(ValueError):rewrite_cpp(r,Path('new.cpp'),Path('new.o'))
    def test_output_must_be_unique(self):
        r=self.receipt();r['arguments']+=['-o','other.o']
        with self.assertRaises(ValueError):rewrite_cpp(r,Path('new.cpp'),Path('new.o'))
    def test_unknown_attached_output_rejected(self):
        r=self.receipt();r['arguments']+=['-MJold.json']
        with self.assertRaises(ValueError):rewrite_cpp(r,Path('new.cpp'),Path('new.o'))
    def test_noncompiler_and_nonkernel_rejected(self):
        r=self.receipt();r['arguments'][0]='/bin/sh'
        with self.assertRaises(ValueError):rewrite_cpp(r,Path('new.cpp'),Path('new.o'))
        r=self.receipt();r['arguments'].remove('-DKERNEL=1')
        with self.assertRaises(ValueError):rewrite_cpp(r,Path('new.cpp'),Path('new.o'))

class ConfigurationTests(unittest.TestCase):
    def test_overlay_guards_call_without_stub_and_preserves_original_config(self):
        with TemporaryDirectory() as temp:
            root=HostPath(temp);core=root/'core';device=root/'device'
            (core/'patches/xnu').mkdir(parents=True);(device/'configs').mkdir(parents=True)
            original='CONFIG_XNU_BOOT_PROGRESS=y\nCONFIG_XNU_NATIVE_UFS=n\n'
            (device/'configs/fuxi_defconfig').write_text(original)
            patch=core/'patches/xnu/offline-component-load-probes.patch'
            patch.write_text('--- a/test.c\n+++ b/test.c\n@@ -1,1 +1,4 @@\n context\n+\tint legacy_register = 0;\n+\tnative_legacy_display_probe();\n+\tint client_probe = 0;\n')
            result=prepare_ci_inputs(core,device)
            self.assertEqual((device/'configs/fuxi_defconfig').read_text(),original)
            self.assertEqual((device/'configs/fuxi_ci_defconfig').read_text(),original.replace('=y','=n'))
            text=patch.read_text();self.assertIn('@@ -1,1 +1,6 @@',text)
            self.assertIn('+#if defined(XNU_NATIVE_LEGACY_DISPLAY)',text)
            self.assertIn('+\tnative_legacy_display_probe();',text)
            self.assertFalse(result['hardware_features_added'])
if __name__=='__main__':unittest.main()
