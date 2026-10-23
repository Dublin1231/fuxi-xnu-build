"""Compiler receipt rewrite safeguards; these do not emulate an SDK build."""
from pathlib import PurePosixPath as Path
import unittest
from investigate import rewrite_cpp
from investigate import prepare_ci_inputs
from investigate import CI_ARCHIVES, EXCLUDED_ARCHIVES
import json
import tomllib
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
    def fixture(self,root):
        core=root/'core';device=root/'device'
        (core/'patches/xnu').mkdir(parents=True);(device/'configs').mkdir(parents=True)
        original='CONFIG_XNU_BOOT_PROGRESS=y\nCONFIG_XNU_NATIVE_UFS=n\n'
        original+=''.join('CONFIG_XNU_'+f+'=y\n' for f in
            ['SOURCE_CORECRYPTO14','SOURCE_PTHREAD','ARM_DT_PLATFORM','ARMCPU','NATIVE_AES'])
        original+='CONFIG_XNU_EXTRA_LD_ARCHIVES="{tree}/build/legacy-stub/libLegacyStub.a"\n'
        (device/'configs/fuxi_defconfig').write_text(original)
        entries=CI_ARCHIVES[:5]+EXCLUDED_ARCHIVES[:2]+[CI_ARCHIVES[5],EXCLUDED_ARCHIVES[2],CI_ARCHIVES[6],EXCLUDED_ARCHIVES[3]]
        (device/'board.toml').write_text('[board]\nname="fuxi"\n[kernel]\narchives = [\n'+
            ''.join(json.dumps(e)+',\n' for e in entries)+']\n[kernel.prelink]\nslide="-0x1000000"\n')
        patch=core/'patches/xnu/offline-component-load-probes.patch'
        patch.write_text('--- a/test.c\n+++ b/test.c\n@@ -1,1 +1,4 @@\n context\n+\tint legacy_register = 0;\n+\tnative_legacy_display_probe();\n+\tint client_probe = 0;\n')
        return core,device,original,patch

    def test_overlay_guards_call_without_stub_and_preserves_original_config(self):
        with TemporaryDirectory() as temp:
            core,device,original,patch=self.fixture(HostPath(temp))
            before=tomllib.loads((device/'board.toml').read_text())
            result=prepare_ci_inputs(core,device)
            self.assertEqual((device/'configs/fuxi_defconfig').read_text(),original)
            derived=(device/'configs/fuxi_ci_defconfig').read_text()
            self.assertIn('CONFIG_XNU_SOURCE_CORECRYPTO14=y',derived)
            self.assertIn('CONFIG_XNU_BUILD_ARCHIVES=y',derived)
            self.assertIn('CONFIG_XNU_EXTRA_LD_ARCHIVES=""',derived)
            self.assertIn('CONFIG_XNU_BOOT_PROGRESS=n',derived)
            after=tomllib.loads((device/'board.toml').read_text())
            self.assertEqual(after['kernel']['archives'],CI_ARCHIVES)
            before['kernel']['archives']=CI_ARCHIVES
            self.assertEqual(before,after)
            text=patch.read_text();self.assertIn('@@ -1,1 +1,6 @@',text)
            self.assertIn('+#if defined(XNU_NATIVE_LEGACY_DISPLAY)',text)
            self.assertIn('+\tnative_legacy_display_probe();',text)
            self.assertFalse(result['hardware_features_added'])
            self.assertFalse(result['crypto_replaced_with_stub'])

    def test_unknown_archive_rejected_without_board_or_patch_mutation(self):
        with TemporaryDirectory() as temp:
            core,device,_,patch=self.fixture(HostPath(temp))
            board=device/'board.toml'
            board.write_text(board.read_text().replace('build-corecrypto-14.py"','unknown-builder.py"',1))
            before=(board.read_bytes(),patch.read_bytes())
            with self.assertRaisesRegex(ValueError,'Unexpected upstream archive plan'):prepare_ci_inputs(core,device)
            self.assertEqual(before,(board.read_bytes(),patch.read_bytes()))
            self.assertFalse((device/'configs/fuxi_ci_defconfig').exists())

    def test_enabled_storage_or_disabled_crypto_rejected(self):
        for old,new in [('CONFIG_XNU_NATIVE_UFS=n','CONFIG_XNU_NATIVE_UFS=y'),
                        ('CONFIG_XNU_SOURCE_CORECRYPTO14=y','CONFIG_XNU_SOURCE_CORECRYPTO14=n')]:
            with self.subTest(feature=old),TemporaryDirectory() as temp:
                core,device,_,_=self.fixture(HostPath(temp))
                config=device/'configs/fuxi_defconfig';config.write_text(config.read_text().replace(old,new))
                with self.assertRaises(ValueError):prepare_ci_inputs(core,device)
if __name__=='__main__':unittest.main()
