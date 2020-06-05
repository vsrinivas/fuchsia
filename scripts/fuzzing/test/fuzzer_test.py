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

    def test_filter(self):
        host = FakeHost()
        fuzzers = host.fuzzers
        self.assertEqual(len(Fuzzer.filter(fuzzers, '')), 6)
        self.assertEqual(len(Fuzzer.filter(fuzzers, '/')), 6)
        self.assertEqual(len(Fuzzer.filter(fuzzers, 'fake')), 6)
        self.assertEqual(len(Fuzzer.filter(fuzzers, 'package1')), 3)
        self.assertEqual(len(Fuzzer.filter(fuzzers, 'target1')), 3)
        self.assertEqual(len(Fuzzer.filter(fuzzers, 'package2/target1')), 2)
        self.assertEqual(
            len(Fuzzer.filter(fuzzers, 'fake-package2/fake-target1')), 1)
        self.assertEqual(len(Fuzzer.filter(fuzzers, '1/2')), 1)
        self.assertEqual(len(Fuzzer.filter(fuzzers, 'target4')), 0)
        with self.assertRaises(Fuzzer.NameError):
            Fuzzer.filter(fuzzers, 'a/b/c')

    def test_from_args(self):
        device = FakeDevice()
        parser = ArgParser('test_from_args')
        with self.assertRaises(SystemExit):
            args = parser.parse_args(['target4'])
            fuzzer = Fuzzer.from_args(device, args)

    def test_measure_corpus(self):
        fuzzer = Fuzzer(FakeDevice(), u'fake-package1', u'fake-target1')
        sizes = fuzzer.measure_corpus()
        self.assertEqual(sizes[0], 2)
        self.assertEqual(sizes[1], 1796 + 124)

    def test_list_artifacts(self):
        fuzzer = Fuzzer(FakeDevice(), u'fake-package1', u'fake-target1')
        artifacts = fuzzer.list_artifacts()
        self.assertEqual(len(artifacts), 3)
        self.assertTrue('crash-deadbeef' in artifacts)
        self.assertTrue('leak-deadfa11' in artifacts)
        self.assertTrue('oom-feedface' in artifacts)
        self.assertFalse('fuzz-0.log' in artifacts)

    def test_is_running(self):
        device = FakeDevice()
        fuzzer1 = Fuzzer(device, u'fake-package1', u'fake-target1')
        fuzzer2 = Fuzzer(device, u'fake-package1', u'fake-target2')
        fuzzer3 = Fuzzer(device, u'fake-package1', u'fake-target3')
        self.assertTrue(fuzzer1.is_running())
        self.assertTrue(fuzzer2.is_running())
        self.assertFalse(fuzzer2.is_running())
        self.assertFalse(fuzzer3.is_running())

    def test_require_stopped(self):
        device = FakeDevice()
        fuzzer1 = Fuzzer(device, u'fake-package1', u'fake-target1')
        fuzzer2 = Fuzzer(device, u'fake-package1', u'fake-target2')
        fuzzer3 = Fuzzer(device, u'fake-package1', u'fake-target3')
        with self.assertRaises(Fuzzer.StateError):
            fuzzer1.require_stopped()
        with self.assertRaises(Fuzzer.StateError):
            fuzzer2.require_stopped()
        fuzzer2.require_stopped()
        fuzzer3.require_stopped()

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
            fuzzer.add_libfuzzer_opts(libfuzzer_opts)
            fuzzer.add_libfuzzer_args(libfuzzer_args)
            fuzzer.add_subprocess_args(subprocess_args)
            fuzzer.start()

        finally:
            shutil.rmtree(base_dir)
        if expected:
            self.assertIn(
                ' '.join(
                    device.get_ssh_cmd(
                        [
                            'ssh',
                            '::1',
                            'run',
                            fuzzer.url(),
                        ] + expected)), device.host.history)

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
                '-jobs=0',
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
        device.host.mkdir(os.path.join(fuzzer._output, 'latest'))
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
        pids = device.getpids()
        fuzzer1 = Fuzzer(device, u'fake-package1', u'fake-target1')
        fuzzer1.stop()
        self.assertIn(
            ' '.join(
                device.get_ssh_cmd(
                    [
                        'ssh',
                        '::1',
                        'kill',
                        str(pids[str(fuzzer1)]),
                    ])), device.host.history)
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
        fuzzer.add_libfuzzer_opts(libfuzzer_opts)
        fuzzer.add_libfuzzer_args(libfuzzer_args)
        fuzzer.add_subprocess_args(subprocess_args)
        artifacts = ['data/' + artifact for artifact in fuzzer.list_artifacts()]
        fuzzer.repro()
        self.assertIn(
            ' '.join(
                device.get_ssh_cmd(
                    [
                        'ssh',
                        '::1',
                        'run',
                        fuzzer.url(),
                        '-artifact_prefix=data/',
                        '-jobs=0',
                        '-some-lf-arg=value',
                    ] + artifacts)), device.host.history)

    def test_merge(self):
        device = FakeDevice()
        parser = ArgParser('test_merge')
        args, libfuzzer_opts, libfuzzer_args, subprocess_args = parser.parse(
            [
                'package1/target2',
                '-some-lf-arg=value',
            ])
        fuzzer = Fuzzer.from_args(device, args)
        fuzzer.add_libfuzzer_opts(libfuzzer_opts)
        fuzzer.add_libfuzzer_args(libfuzzer_args)
        fuzzer.add_subprocess_args(subprocess_args)
        fuzzer.merge()
        self.assertIn(
            ' '.join(
                device.get_ssh_cmd(
                    [
                        'ssh',
                        '::1',
                        'run',
                        fuzzer.url(),
                        '-artifact_prefix=data/',
                        '-jobs=0',
                        '-merge=1',
                        '-merge_control_file=data/.mergefile',
                        '-some-lf-arg=value',
                        'data/corpus/',
                        'data/corpus.prev/',
                    ])), device.host.history)


if __name__ == '__main__':
    unittest.main()
