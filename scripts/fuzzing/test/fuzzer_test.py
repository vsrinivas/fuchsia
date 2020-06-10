#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import tempfile
import unittest

import test_env
from lib.factory import Factory
from lib.fuzzer import Fuzzer

from device_test import DeviceTestCase

from cli_fake import FakeCLI
from device_fake import FakeDevice
from host_fake import FakeHost
from fuzzer_fake import FakeFuzzer
from factory_fake import FakeFactory


class TestFuzzer(DeviceTestCase):

    def test_measure_corpus(self):
        device = FakeDevice()
        fuzzer = Fuzzer(device, u'fake-package1', u'fake-target1')
        cmd = ['ls', '-l', fuzzer.data_path('corpus')]
        device.add_ssh_response(
            cmd, [
                '-rw-r--r-- 1 0 0 1796 Mar 19 17:25 feac37187e77ff60222325cf2829e2273e04f2ea',
                '-rw-r--r-- 1 0 0  124 Mar 18 22:02 ff415bddb30e9904bccbbd21fb5d4aa9bae9e5a5',
            ])
        sizes = fuzzer.measure_corpus()
        self.assertEqual(sizes, (2, 1796 + 124))

    def test_list_artifacts(self):
        device = FakeDevice()
        fuzzer = Fuzzer(device, u'fake-package1', u'fake-target1')
        cmd = ['ls', '-l', fuzzer.data_path()]
        device.add_ssh_response(
            cmd, [
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

        self.assertFalse(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

        device.add_fake_pid(fuzzer1.package, fuzzer1.executable)
        self.assertTrue(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

        device.add_fake_pid(fuzzer2.package, fuzzer2.executable)
        self.assertTrue(fuzzer1.is_running())
        self.assertTrue(fuzzer2.is_running())

        # Results are cached until refresh
        device.clear_fake_pids(refresh=False)
        self.assertTrue(fuzzer1.is_running())
        self.assertTrue(fuzzer2.is_running())

        device.clear_fake_pids(refresh=True)
        self.assertFalse(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

    def test_require_stopped(self):
        device = FakeDevice()
        fuzzer = Fuzzer(device, u'fake-package1', u'fake-target1')
        fuzzer.require_stopped()
        device.add_fake_pid(fuzzer.package, fuzzer.executable)
        with self.assertRaises(SystemExit):
            fuzzer.require_stopped()
        self.assertEqual(
            device.host.cli.log, [
                'ERROR: fake-package1/fake-target1 is running and must be stopped first.',
            ])
        device.clear_fake_pids()
        fuzzer.require_stopped(refresh=True)

    # Helper to test Fuzzer.start with different options and arguments. The
    # expected options and arguments to be passed to libFuzzer are given by
    # extra_args. If the helper is expected to raise an exception, leave
    # `expected` empty and have the caller handle the exception.
    def start_helper(self, factory, extra_args, expected):
        base_dir = tempfile.mkdtemp()
        try:
            parser = factory.create_parser()
            args = ['start', '--output', base_dir, 'package1/target2']
            args += extra_args
            args = parser.parse_args(args)
            fuzzer = factory.create_fuzzer(args)
            fuzzer.start()
        finally:
            shutil.rmtree(base_dir)
        if expected:
            self.assertSsh(factory.device, 'run', fuzzer.url(), *expected)

    def test_start(self):
        factory = FakeFactory()
        self.start_helper(
            factory, [], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                '-jobs=1',
                'data/corpus/',
            ])

    def test_start_foreground(self):
        factory = FakeFactory()
        self.start_helper(
            factory, ['--foreground'], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                'data/corpus/',
            ])

    def test_start_debug(self):
        factory = FakeFactory()
        self.start_helper(
            factory, ['--debug'], [
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
        factory = FakeFactory()
        self.start_helper(
            factory, ['-option2=foo', '-option1=\'bar baz\''], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                '-jobs=1',
                '-option1=\'bar baz\'',
                '-option2=foo',
                'data/corpus/',
            ])

    def test_start_with_bad_option(self):
        factory = FakeFactory()
        with self.assertRaises(SystemExit):
            self.start_helper(factory, ['-option2=foo', '-option1'], None)

    def test_start_with_passthrough(self):
        factory = FakeFactory()
        self.start_helper(
            factory, ['-option2=foo', '--', '-option1=bar'], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                '-jobs=1',
                '-option2=foo',
                'data/corpus/',
                '--',
                '-option1=bar',
            ])

    # Helper to test Fuzzer.symbolize with different logs.
    def symbolize_helper(self, factory, log_in, log_out, echo=False):
        cmd = [
            factory.host.symbolizer_exec, '-llvm-symbolizer',
            factory.host.llvm_symbolizer
        ]
        for build_id_dir in factory.host.build_id_dirs:
            cmd += ['-build-id-dir', build_id_dir]
        cmd = ' '.join(cmd)
        factory.host.responses[cmd] = [
            "[000001.234567][123][456][klog] INFO: Symbolized line 1",
            "[000001.234568][123][456][klog] INFO: Symbolized line 2",
            "[000001.234569][123][456][klog] INFO: Symbolized line 3",
        ]
        parser = factory.create_parser()
        args = parser.parse_args(['start', 'package1/target2'])
        fuzzer = factory.create_fuzzer(args)
        factory.host.mkdir(os.path.join(fuzzer._output))
        fuzzer.unsymbolized.write(log_in)
        if fuzzer.symbolize_log(echo=echo):
            self.assertIn(cmd, factory.host.history)
        self.assertEqual(fuzzer.symbolized.read(), log_out)

    def test_symbolize_log_no_mutation_sequence(self):
        factory = FakeFactory()
        self.symbolize_helper(
            factory, """
A line
Another line
Yet another line
""", """
A line
Another line
Yet another line
""")

    def test_symbolize_log_no_process_id(self):
        factory = FakeFactory()
        self.symbolize_helper(
            factory, """
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
        factory = FakeFactory()
        self.symbolize_helper(
            factory, """
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
        factory = FakeFactory()
        self.symbolize_helper(
            factory, """
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

    def test_symbolize_log_with_echo(self):
        factory = FakeFactory()
        self.symbolize_helper(
            factory,
            """
==67890== ERROR: libFuzzer: deadly signal
MS: 1 SomeMutation; base unit: foo
""",
            """
==67890== ERROR: libFuzzer: deadly signal
Symbolized line 1
Symbolized line 2
Symbolized line 3
MS: 1 SomeMutation; base unit: foo
""",
            echo=True)
        self.assertEqual(
            factory.cli.log, [
                '',
                '==67890== ERROR: libFuzzer: deadly signal',
                'Symbolized line 1',
                'Symbolized line 2',
                'Symbolized line 3',
                'MS: 1 SomeMutation; base unit: foo',
            ])

    def test_stop(self):
        factory = FakeFactory()
        parser = factory.create_parser()

        args = parser.parse_args(['stop', 'fake-package1/fake-target1'])
        fuzzer1 = factory.create_fuzzer(args)
        pid = factory.device.add_fake_pid(fuzzer1.package, fuzzer1.executable)
        fuzzer1.stop()
        self.assertSsh(factory.device, 'kill', str(pid))
        args = parser.parse_args(['stop', 'fake-package1/fake-target3'])
        fuzzer3 = factory.create_fuzzer(args)
        fuzzer3.stop()

    def test_repro(self):
        factory = FakeFactory()
        parser = factory.create_parser()
        args = parser.parse_args(
            [
                'repro',
                'package1/target2',
                '-some_lf_arg=value',
                'crash-*',
                'oom-feedface',
            ])

        # Globs should work, but only if they match files
        fuzzer = factory.create_fuzzer(args)
        with self.assertRaises(SystemExit):
            fuzzer.repro()
        self.assertEqual(
            fuzzer.device.host.cli.log, [
                'ERROR: No matching files: "crash-*".',
            ])

        factory.host.pathnames += [
            'crash-deadbeef',
            'crash-deadfa11',
            'oom-feedface',
        ]
        fuzzer.repro()
        self.assertSsh(
            factory.device,
            'run',
            fuzzer.url(),
            '-artifact_prefix=data/',
            '-some_lf_arg=value',
            'data/crash-deadbeef',
            'data/crash-deadfa11',
            'data/oom-feedface',
        )


if __name__ == '__main__':
    unittest.main()
