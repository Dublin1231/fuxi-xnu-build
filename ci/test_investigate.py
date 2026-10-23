"""Compiler receipt rewrite safeguards; these do not emulate an SDK build."""
from pathlib import PurePosixPath as Path
import unittest
from investigate import rewrite_cpp

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

if __name__=='__main__':unittest.main()
