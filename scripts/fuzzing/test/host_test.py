#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import tempfile
import unittest

import test_env
from lib.host import Host

from host_mock import MockHost


class TestHost(unittest.TestCase):

    def test_join(self):
        fuchsia_dir = os.getenv('FUCHSIA_DIR')
        os.unsetenv('FUCHSIA_DIR')
        del os.environ['FUCHSIA_DIR']
        with self.assertRaises(Host.ConfigError):
            Host.join('foo')
        if not fuchsia_dir:
            return
        os.environ['FUCHSIA_DIR'] = fuchsia_dir
        self.assertTrue(Host.join('foo').endswith('foo'))

    def test_find_build_dir(self):
        host = Host()
        fuchsia_dir = os.getenv('FUCHSIA_DIR')
        os.unsetenv('FUCHSIA_DIR')
        del os.environ['FUCHSIA_DIR']
        with self.assertRaises(Host.ConfigError):
            host.find_build_dir()
        if not fuchsia_dir:
            return
        os.environ['FUCHSIA_DIR'] = fuchsia_dir
        print(host.find_build_dir())
        self.assertTrue(os.path.isdir(host.find_build_dir()))

    def test_add_build_ids(self):
        host = Host()
        host.add_build_ids('no_such_ids')
        self.assertNotIn('no_such_ids', host._ids)
        if not os.getenv('FUCHSIA_DIR'):
            return
        build_dir = host.find_build_dir()
        build_ids = Host.join(build_dir, '.build-id')
        if not os.path.exists(build_ids):
            return
        host.add_build_ids(build_ids)
        self.assertIn(build_ids, host._ids)

    def test_set_zxtools(self):
        host = Host()
        with self.assertRaises(Host.ConfigError):
            host.set_zxtools('no_such_zxtools')
        if not os.getenv('FUCHSIA_DIR'):
            return
        build_dir = host.find_build_dir()
        zxtools = Host.join(build_dir + '.zircon', 'tools')
        if not os.path.isdir(zxtools):
            return
        host.set_zxtools(zxtools)
        self.assertEqual(host._zxtools, zxtools)

    def test_set_symbolizer(self):
        host = Host()
        with self.assertRaises(Host.ConfigError):
            host.set_symbolizer('no_such_executable', 'no_such_symbolizer')
        if not os.getenv('FUCHSIA_DIR'):
            return
        platform = 'mac-x64' if os.uname()[0] == 'Darwin' else 'linux-x64'
        executable = Host.join(
            host.get_host_out_dir(), 'symbolize')
        symbolizer = Host.join(
            'prebuilt', 'third_party', 'clang', platform, 'bin',
            'llvm-symbolizer')
        with self.assertRaises(Host.ConfigError):
            host.set_symbolizer(executable, 'no_such_symbolizer')
        with self.assertRaises(Host.ConfigError):
            host.set_symbolizer('no_such_executable', symbolizer)
        host.set_symbolizer(executable, symbolizer)
        self.assertEqual(host._symbolizer_exec, executable)
        self.assertEqual(host._llvm_symbolizer, symbolizer)

    def test_set_fuzzers_json(self):
        host = Host()
        with self.assertRaises(Host.ConfigError):
            host.set_fuzzers_json('no_such_json')
        if not os.getenv('FUCHSIA_DIR'):
            return
        build_dir = host.find_build_dir()
        json_file = Host.join(build_dir, 'fuzzers.json')
        # No guarantee of contents; just ensure it parses without crashing.
        if os.path.exists(json_file):
            host.set_fuzzers_json(json_file)
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
        with tempfile.NamedTemporaryFile() as f:
            f.write(json.dumps(data))
            f.seek(0)
            host.set_fuzzers_json(f.name)
            self.assertIn(('foo_fuzzers', 'foo_fuzzer'), host.fuzzers)
            self.assertIn(('zircon_fuzzers', 'zx_fuzzer.asan'), host.fuzzers)
            self.assertIn(('zircon_fuzzers', 'zx_fuzzer.ubsan'), host.fuzzers)

    def test_set_build_dir(self):
        host = Host()
        with self.assertRaises(Host.ConfigError):
            host.set_fuzzers_json('no_such_build_dir')
        if not os.getenv('FUCHSIA_DIR'):
            return
        build_dir = host.find_build_dir()
        ssh_config = Host.join(build_dir, 'ssh-keys', 'ssh_config')
        zxtools = Host.join(build_dir + '.zircon', 'tools')
        platform = 'mac-x64' if os.uname()[0] == 'Darwin' else 'linux-x64'
        executable = Host.join(
            host.get_host_out_dir(), 'symbolize')
        symbolizer = Host.join(
            'prebuilt', 'third_party', 'clang', platform, 'bin',
            'llvm-symbolizer')
        host.set_build_dir(build_dir)
        self.assertNotEqual(len(host._ids), 0)
        for id in host._ids:
            self.assertTrue(os.path.exists(id))
        self.assertIsNotNone(host._zxtools)
        self.assertTrue(os.path.exists(host._symbolizer_exec))
        self.assertTrue(os.path.exists(host._llvm_symbolizer))

    def test_zircon_tool(self):
        # If a build tree is available, try using a zircon tool for "real".
        host = Host()
        try:
            host.set_build_dir(host.find_build_dir())
        except Host.ConfigError:
            return
        with self.assertRaises(Host.ConfigError):
            host.zircon_tool(['no_such_tool'])
        path = os.path.abspath(__file__)
        line = host.zircon_tool(['merkleroot', path])
        self.assertRegexpMatches(line, r'[0-9a-f]* - ' + path)

    def test_killall(self):
        mock = MockHost()
        mock.killall('mock_tool')
        self.assertIn('killall mock_tool', mock.history)

    def test_symbolize(self):
        mock = MockHost()
        stacktrace = ['a line', 'another line', 'yet another line']
        mock.symbolize('\n'.join(stacktrace))
        self.assertIn(
            ' '.join(
                [
                    'mock/symbolize', '-llvm-symbolizer',
                    'mock/llvm_symbolizer', '-build-id-dir', 'mock/.build-id'
                ]), mock.history)
        for line in stacktrace:
            for c in line:
                self.assertIn(' < ' + c, mock.history)

    def test_notify_user(self):
        host = Host()
        try:
            host.set_build_dir(host.find_build_dir())
        except Host.ConfigError:
            return
        host.notify_user('This is a test', 'This is only a test.')

    def test_snapshot(self):
        fuchsia_dir = os.getenv('FUCHSIA_DIR')
        os.unsetenv('FUCHSIA_DIR')
        del os.environ['FUCHSIA_DIR']
        host = Host()
        with self.assertRaises(Host.ConfigError):
            host.snapshot()
        if not fuchsia_dir:
            return
        os.environ['FUCHSIA_DIR'] = fuchsia_dir
        line = host.snapshot()
        self.assertRegexpMatches(line, r'[0-9a-f]{40}')


if __name__ == '__main__':
    unittest.main()
