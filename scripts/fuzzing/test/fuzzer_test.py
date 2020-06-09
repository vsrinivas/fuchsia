#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import tempfile
import unittest

import test_env
from lib.args import ArgParser
from lib.fuzzer import Fuzzer

from device_fake import FakeDevice
from host_fake import FakeHost
from fuzzer_fake import FakeFuzzer


class TestFuzzer(unittest.TestCase):

    # Unit test assertions

    def assertRan(self, host, *args):
        self.assertIn(' '.join(args), host.history)

    def assertSsh(self, device, *args):
        """Asserts a previous call was made to device.scp with cmd."""
        self.assertRan(device.host, *device._ssh_cmd(list(args)))

    # Unit tests

    def test_from_args(self):
        device = FakeDevice()
        parser = ArgParser('test_from_args')
        with self.assertRaises(SystemExit):
            args = parser.parse_args(['target4'])
            fuzzer = Fuzzer.from_args(device, args)

    def test_measure_corpus(self):
        device = FakeDevice()
        fuzzer = Fuzzer(device, u'fake-package1', u'fake-target1')
        device.add_ssh_response(
            device._ls_cmd(fuzzer.data_path('corpus')), [
                '-rw-r--r-- 1 0 0 1796 Mar 19 17:25 feac37187e77ff60222325cf2829e2273e04f2ea',
                '-rw-r--r-- 1 0 0  124 Mar 18 22:02 ff415bddb30e9904bccbbd21fb5d4aa9bae9e5a5',
            ])
        sizes = fuzzer.measure_corpus()
        self.assertEqual(sizes, (2, 1796 + 124))

    def test_list_artifacts(self):
        device = FakeDevice()
        fuzzer = Fuzzer(device, u'fake-package1', u'fake-target1')
        device.add_ssh_response(
            device._ls_cmd(fuzzer.data_path()), [
                'drw-r--r-- 2 0 0 13552 Mar 20 01:40 corpus',
                '-rw-r--r-- 1 0 0   918 Mar 20 01:40 fuzz-0.log',
                '-rw-r--r-- 1 0 0  1337 Mar 20 01:40 crash-deadbeef',
                '-rw-r--r-- 1 0 0  1729 Mar 20 01:40 leak-deadfa11',
                '-rw-r--r-- 1 0 0 31415 Mar 20 01:40 oom-feedface',
            ])
        artifacts = fuzzer.list_artifacts()
        self.assertEqual(len(artifacts), 3)
        self.assertIn('crash-deadbeef', artifacts)
        self.assertIn('leak-deadfa11', artifacts)
        self.assertIn('oom-feedface', artifacts)
        self.assertNotIn('fuzz-0.log', artifacts)

    def test_is_running(self):
        device = FakeDevice()
        fuzzer1 = Fuzzer(device, u'fake-package1', u'fake-target1')
        fuzzer2 = Fuzzer(device, u'fake-package1', u'fake-target2')

        # Initially stopped
        self.assertFalse(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

        # Previous results cached
        device.add_fake_pid(fuzzer1.package, fuzzer1.executable)
        device.add_fake_pid(fuzzer2.package, fuzzer2.executable)
        self.assertFalse(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

        # Fresh status can be requested.
        self.assertTrue(fuzzer1.is_running(refresh=True))
        self.assertTrue(fuzzer2.is_running(refresh=True))

    def test_require_stopped(self):
        device = FakeDevice()
        fuzzer = Fuzzer(device, u'fake-package1', u'fake-target1')
        fuzzer.require_stopped()
        device.add_fake_pid(fuzzer.package, fuzzer.executable)
        with self.assertRaises(RuntimeError):
            fuzzer.require_stopped(refresh=True)
        device.clear_fake_pids()
        fuzzer.require_stopped(refresh=True)

    # Helper to test Fuzzer.start with different options and arguments. The expected options and
    # arguments to be passed to libFuzzer are given by lf_args. If the helper is expected to raise
    # an exception, leave `expected` empty and have the caller handle the exception.
    def start_helper(self, args, expected):
        device = FakeDevice()
        base_dir = tempfile.mkdtemp()
        try:
            parser = ArgParser('start_helper')
            args, libfuzzer_opts, libfuzzer_args, subprocess_args = parser.parse(
                ['package1/target2'] + args)
            args.output = base_dir
            fuzzer = FakeFuzzer.from_args(device, args)
            fuzzer.libfuzzer_opts = libfuzzer_opts
            fuzzer.libfuzzer_args = libfuzzer_args
            fuzzer.subprocess_args = subprocess_args
            fuzzer.start()

        finally:
            shutil.rmtree(base_dir)
        if expected:
            self.assertSsh(device, 'run', fuzzer.url(), *expected)

    def test_start(self):
        self.start_helper(
            [], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                '-jobs=1',
                'data/corpus/',
            ])

    def test_start_foreground(self):
        self.start_helper(
            ['--foreground'], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                'data/corpus/',
            ])

    def test_start_debug(self):
        self.start_helper(
            ['--debug'], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                '-handle_abrt=0',
                '-handle_bus=0',
                '-handle_fpe=0',
                '-handle_ill=0',
                '-handle_segv=0',
                '-jobs=1',
                'data/corpus/',
            ])

    def test_start_with_options(self):
        self.start_helper(
            ['-option2=foo', '-option1=\'bar baz\''], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                '-jobs=1',
                '-option1=\'bar baz\'',
                '-option2=foo',
                'data/corpus/',
            ])

    def test_start_with_bad_option(self):
        with self.assertRaises(ValueError):
            self.start_helper(['-option2=foo', '-option1'], None)

    def test_start_with_bad_corpus(self):
        with self.assertRaises(ValueError):
            self.start_helper(['custom-corpus'], None)

    def test_start_with_passthrough(self):
        self.start_helper(
            ['-option2=foo', '--', '-option1=bar'], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                '-jobs=1',
                '-option2=foo',
                'data/corpus/',
                '--',
                '-option1=bar',
            ])

    # Helper to test Fuzzer.symbolize with different logs.
    def symbolize_helper(self, log_in, log_out):
        device = FakeDevice()
        host = device.host
        cmd = ' '.join(host._symbolizer_cmd())
        host.responses[cmd] = [
            "[000001.234567][123][456][klog] INFO: Symbolized line 1",
            "[000001.234568][123][456][klog] INFO: Symbolized line 2",
            "[000001.234569][123][456][klog] INFO: Symbolized line 3",
        ]
        fuzzer = FakeFuzzer(device, u'fake-package1', u'fake-target2')
        device.host.mkdir(os.path.join(fuzzer._output))
        fuzzer.unsymbolized.write(log_in)
        if fuzzer.symbolize_log():
            self.assertIn(cmd, host.history)
        self.assertEqual(fuzzer.symbolized.read(), log_out)

    def test_symbolize_log_no_mutation_sequence(self):
        self.symbolize_helper(
            """
A line
Another line
Yet another line
""", """
A line
Another line
Yet another line
""")

    def test_symbolize_log_no_process_id(self):
        self.symbolize_helper(
            """
A line
Another line
MS: 1 SomeMutation; base unit: foo
Yet another line
artifact_prefix='data/'; Test unit written to data/crash-aaaa
""", """
A line
Another line
Symbolized line 1
Symbolized line 2
Symbolized line 3
MS: 1 SomeMutation; base unit: foo
Yet another line
artifact_prefix='data/'; Test unit written to data/crash-aaaa
""")

    def test_symbolize_log_pid_from_stacktrace(self):
        self.symbolize_helper(
            """
A line
Another line
==12345== INFO: libfuzzer stack trace:
MS: 1 SomeMutation; base unit: foo
Yet another line
artifact_prefix='data/'; Test unit written to data/crash-bbbb
""", """
A line
Another line
==12345== INFO: libfuzzer stack trace:
Symbolized line 1
Symbolized line 2
Symbolized line 3
MS: 1 SomeMutation; base unit: foo
Yet another line
artifact_prefix='data/'; Test unit written to data/crash-bbbb
""")

    def test_symbolize_log_pid_from_deadly_signal(self):
        self.symbolize_helper(
            """
A line
Another line
==67890== ERROR: libFuzzer: deadly signal
MS: 1 SomeMutation; base unit: foo
Yet another line
artifact_prefix='data/'; Test unit written to data/crash-cccc
""", """
A line
Another line
==67890== ERROR: libFuzzer: deadly signal
Symbolized line 1
Symbolized line 2
Symbolized line 3
MS: 1 SomeMutation; base unit: foo
Yet another line
artifact_prefix='data/'; Test unit written to data/crash-cccc
""")

    def test_stop(self):
        device = FakeDevice()
        fuzzer1 = Fuzzer(device, u'fake-package1', u'fake-target1')
        pid = device.add_fake_pid(fuzzer1.package, fuzzer1.executable)
        fuzzer1.stop()
        self.assertSsh(device, 'kill', str(pid))
        fuzzer3 = Fuzzer(device, u'fake-package1', u'fake-target3')
        fuzzer3.stop()

    def test_repro(self):
        device = FakeDevice()
        parser = ArgParser('test_repro')
        args, libfuzzer_opts, libfuzzer_args, subprocess_args = parser.parse(
            [
                'package1/target2',
                '-some-lf-arg=value',
            ])
        fuzzer = Fuzzer.from_args(device, args)
        fuzzer.libfuzzer_opts = libfuzzer_opts
        fuzzer.libfuzzer_args = libfuzzer_args
        fuzzer.subprocess_args = subprocess_args

        # No-op if artifacts are empty
        self.assertEqual(fuzzer.repro(), 0)

        device.add_ssh_response(
            device._ls_cmd(fuzzer.data_path()), [
                '-rw-r--r-- 1 0 0  1337 Mar 20 01:40 crash-deadbeef',
                '-rw-r--r-- 1 0 0  1729 Mar 20 01:40 leak-deadfa11',
                '-rw-r--r-- 1 0 0 31415 Mar 20 01:40 oom-feedface',
            ])
        artifacts = ['data/' + artifact for artifact in fuzzer.list_artifacts()]
        self.assertNotEqual(fuzzer.repro(), 0)
        self.assertSsh(
            device, 'run', fuzzer.url(), '-artifact_prefix=data/',
            '-some-lf-arg=value', *artifacts)


if __name__ == '__main__':
    unittest.main()
