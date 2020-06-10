#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import shutil
import tempfile
import unittest

from StringIO import StringIO

import test_env
from lib.host import Host

from cli_fake import FakeCLI
from host_fake import FakeHost


class TestHost(unittest.TestCase):

    # Unit tests

    def test_read_fuzzers(self):
        # Construct and parse both fuchsia and zircon style fuzzer metadata.
        data = [
            {
                'fuzz_host': False,
                'fuzzers': ['foo_fuzzer'],
                'fuzzers_package': 'foo_fuzzers'
            },
            {
                'fuzz_host': False,
                'fuzzers': ['zx_fuzzer.asan', 'zx_fuzzer.ubsan'],
                'fuzzers_package': 'zircon_fuzzers'
            },
        ]
        sio = StringIO()
        sio.write(json.dumps(data))
        sio.seek(0)

        host = FakeHost()
        host.read_fuzzers(sio)
        self.assertIn(('foo_fuzzers', 'foo_fuzzer'), host.fuzzers())
        self.assertIn(('zircon_fuzzers', 'zx_fuzzer.asan'), host.fuzzers())
        self.assertIn(('zircon_fuzzers', 'zx_fuzzer.ubsan'), host.fuzzers())

    def test_fuzzers(self):
        host = FakeHost(autoconfigure=False)
        host.add_fake_fuzzers()
        self.assertEqual(len(host.fuzzers('')), 6)
        self.assertEqual(len(host.fuzzers('/')), 6)
        self.assertEqual(len(host.fuzzers('fake')), 6)
        self.assertEqual(len(host.fuzzers('package1')), 3)
        self.assertEqual(len(host.fuzzers('target1')), 3)
        self.assertEqual(len(host.fuzzers('package2/target1')), 2)
        self.assertEqual(len(host.fuzzers('fake-package2/fake-target1')), 1)
        self.assertEqual(len(host.fuzzers('1/2')), 1)
        self.assertEqual(len(host.fuzzers('target4')), 0)
        with self.assertRaises(ValueError):
            host.fuzzers('a/b/c')

    def test_configure(self):
        host = FakeHost(autoconfigure=False)

        # Accessing these properties without setting them raises errors.
        with self.assertRaises(RuntimeError):
            host.build_dir
        with self.assertRaises(RuntimeError):
            host.symbolizer_exec
        with self.assertRaises(RuntimeError):
            host.llvm_symbolizer
        with self.assertRaises(RuntimeError):
            host.build_id_dirs

        # Fails due to missing paths
        build_dir = 'test_configure'
        with self.assertRaises(ValueError):
            host.configure(build_dir)

        host.add_fake_pathnames(build_dir)
        host.configure(build_dir)
        clang_dir = os.path.join('prebuilt', 'third_party', 'clang', 'fake')

        self.assertEqual(host.build_dir, host.fxpath(build_dir))
        self.assertEqual(
            host.symbolizer_exec,
            host.fxpath(build_dir, 'host_x64', 'symbolize'))
        self.assertEqual(
            host.llvm_symbolizer,
            host.fxpath(clang_dir, 'bin', 'llvm-symbolizer'))
        self.assertEqual(
            host.build_id_dirs, [
                host.fxpath(clang_dir, 'lib', 'debug', '.build-id'),
                host.fxpath(build_dir, '.build-id'),
                host.fxpath(build_dir + '.zircon', '.build-id'),
            ])

    # Filesystem routines.
    # All of these except test_join() use the real Host class and interact with
    # temporary files and directories.

    def test_isdir(self):
        base_dir = tempfile.mkdtemp()
        host = Host(FakeCLI(), base_dir)
        try:
            pathname = os.path.join(base_dir, 'test_isdir')
            self.assertFalse(host.isdir(pathname))
            os.makedirs(pathname)
            self.assertTrue(host.isdir(pathname))
        finally:
            shutil.rmtree(base_dir)

    def test_isfile(self):
        base_dir = tempfile.mkdtemp()
        host = Host(FakeCLI(), base_dir)
        try:
            pathname = os.path.join(base_dir, 'test_isfile')
            self.assertFalse(host.isfile(pathname))
            with open(pathname, 'w') as opened:
                self.assertTrue(host.isfile(pathname))
        finally:
            shutil.rmtree(base_dir)

    def test_mkdir(self):
        base_dir = tempfile.mkdtemp()
        host = Host(FakeCLI(), base_dir)
        try:
            pathname = os.path.join(base_dir, 'test', 'mkdir')
            self.assertFalse(os.path.isdir(pathname))
            host.mkdir(pathname)
            self.assertTrue(os.path.isdir(pathname))
        finally:
            shutil.rmtree(base_dir)

    def test_rmdir(self):
        base_dir = tempfile.mkdtemp()
        host = Host(FakeCLI(), base_dir)
        try:
            pathname = os.path.join(base_dir, 'test_rmdir')
            os.makedirs(pathname)
            self.assertTrue(os.path.isdir(pathname))
            host.rmdir(pathname)
            self.assertFalse(os.path.isdir(pathname))
        finally:
            shutil.rmtree(base_dir)

    def test_link(self):
        base_dir = tempfile.mkdtemp()
        host = Host(FakeCLI(), base_dir)
        try:
            pathname = os.path.join(base_dir, 'test_link')
            foo = os.path.join(base_dir, 'foo')
            bar = os.path.join(base_dir, 'bar')
            self.assertFalse(os.path.islink(pathname))
            with open(foo, 'w') as opened:
                host.link(foo, pathname)
                self.assertTrue(os.path.islink(pathname))
                self.assertEqual(os.readlink(pathname), foo)
            with open(bar, 'w') as opened:
                host.link(bar, pathname)
                self.assertTrue(os.path.islink(pathname))
                self.assertEqual(os.readlink(pathname), bar)
        finally:
            shutil.rmtree(base_dir)

    def test_glob(self):
        base_dir = tempfile.mkdtemp()
        host = Host(FakeCLI(), base_dir)
        try:
            foo = os.path.join(base_dir, 'foo')
            bar = os.path.join(base_dir, 'bar')
            for pathname in [foo, bar]:
                with open(pathname, 'w') as opened:
                    pass
            files = host.glob(os.path.join(base_dir, '*'))
            self.assertIn(foo, files)
            self.assertIn(bar, files)
        finally:
            shutil.rmtree(base_dir)

    # Subprocess routines
    # test_create_process() interacts with a real Host object to create an OS
    # process.

    def test_create_process(self):
        host = Host(FakeCLI(), tempfile.gettempdir())
        p = host.create_process(['true'])
        p.check_call()

    def test_killall(self):
        host = FakeHost()
        host.killall('fake_tool')
        self.assertIn('killall fake_tool', host.history)

    # Other routines

    def test_fxpath(self):
        host = FakeHost(autoconfigure=False)
        self.assertEqual(
            host.fxpath('bar', 'baz'),
            os.path.join(host.fuchsia_dir, 'bar', 'baz'))
        self.assertEqual(
            host.fxpath(host.fuchsia_dir, 'baz'),
            os.path.join(host.fuchsia_dir, 'baz'))

    def test_find_device(self):
        host = FakeHost()
        device_name = 'test_find_device'
        addrs = ['::1', '::2']

        cmd = ' '.join(host._find_device_cmd(device_name))
        host.responses[cmd] = addrs[:1]
        self.assertEqual(host.find_device(device_name=device_name), addrs[0])

        sio = StringIO()
        sio.write(device_name + '\n')
        sio.seek(0)
        self.assertEqual(host.find_device(device_file=sio), addrs[0])

        # No results from `fx device-finder list`
        with self.assertRaises(RuntimeError):
            host.find_device()

        # Multiple results from `fx device-finder list`
        cmd = ' '.join(host._find_device_cmd())
        host.responses[cmd] = addrs
        with self.assertRaises(RuntimeError):
            host.find_device()

        host.responses[cmd] = addrs[:1]
        self.assertEqual(host.find_device(), addrs[0])

    def test_symbolize(self):
        host = FakeHost()
        stacktrace = [
            'a line',
            'another line',
            'yet another line',
        ]
        cmd = ' '.join(
            [
                host.symbolizer_exec,
                '-llvm-symbolizer',
                host.llvm_symbolizer,
            ])
        cmd = ' -build-id-dir '.join([cmd] + host.build_id_dirs)
        host.responses[cmd] = [
            '[000001.234567][123][456][klog] INFO: Symbolized line 1',
            '[000001.234568][123][456][klog] INFO: Symbolized line 2',
            '[000001.234569][123][456][klog] INFO: Symbolized line 3',
        ]
        symbolized = host.symbolize('\n'.join(stacktrace))
        self.assertIn(cmd, host.history)
        for line in stacktrace:
            self.assertIn(host.as_input(line), host.history)
        self.assertEqual(
            symbolized.strip().split('\n'), [
                'Symbolized line 1',
                'Symbolized line 2',
                'Symbolized line 3',
            ])

    def test_snapshot(self):
        host = FakeHost()
        cmd = ' '.join(['git', 'rev-parse', 'HEAD'])
        line = host.snapshot()
        self.assertIn(
            host.with_cwd(cmd, host.fxpath('integration')), host.history)


if __name__ == '__main__':
    unittest.main()
