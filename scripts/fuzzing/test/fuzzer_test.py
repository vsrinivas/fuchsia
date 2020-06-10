#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

import test_env
from test_case import TestCase


class FuzzerTest(TestCase):

    def test_measure_corpus(self):
        fuzzer = self.create_fuzzer('check', 'fake-package1/fake-target1')
        pathname = fuzzer.data_path('corpus')
        cmd = ['ls', '-l', fuzzer.data_path('corpus')]
        self.set_outputs(
            cmd, [
                '-rw-r--r-- 1 0 0 1796 Mar 19 17:25 feac37187e77ff60222325cf2829e2273e04f2ea',
                '-rw-r--r-- 1 0 0  124 Mar 18 22:02 ff415bddb30e9904bccbbd21fb5d4aa9bae9e5a5',
            ],
            ssh=True)
        sizes = fuzzer.measure_corpus()
        self.assertEqual(sizes, (2, 1796 + 124))

    def test_list_artifacts(self):
        fuzzer = self.create_fuzzer('check', 'fake-package1/fake-target1')
        cmd = ['ls', '-l', fuzzer.data_path()]
        self.set_outputs(
            cmd, [
                'drw-r--r-- 2 0 0 13552 Mar 20 01:40 corpus',
                '-rw-r--r-- 1 0 0   918 Mar 20 01:40 fuzz-0.log',
                '-rw-r--r-- 1 0 0  1337 Mar 20 01:40 crash-deadbeef',
                '-rw-r--r-- 1 0 0  1729 Mar 20 01:40 leak-deadfa11',
                '-rw-r--r-- 1 0 0 31415 Mar 20 01:40 oom-feedface',
            ],
            ssh=True)
        artifacts = fuzzer.list_artifacts()
        self.assertEqual(len(artifacts), 3)
        self.assertIn('crash-deadbeef', artifacts)
        self.assertIn('leak-deadfa11', artifacts)
        self.assertIn('oom-feedface', artifacts)
        self.assertNotIn('fuzz-0.log', artifacts)

    def test_is_running(self):
        fuzzer1 = self.create_fuzzer('check', 'fake-package1/fake-target1')
        fuzzer2 = self.create_fuzzer('check', 'fake-package1/fake-target2')

        self.assertFalse(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

        self.set_running(fuzzer1.package, fuzzer1.executable)
        self.assertTrue(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

        self.set_running(fuzzer2.package, fuzzer2.executable)
        self.assertTrue(fuzzer1.is_running())
        self.assertTrue(fuzzer2.is_running())

        # Results are cached until refresh
        self.stop_all(refresh=False)
        self.assertTrue(fuzzer1.is_running())
        self.assertTrue(fuzzer2.is_running())

        self.stop_all(refresh=True)
        self.assertFalse(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

    def test_require_stopped(self):
        fuzzer = self.create_fuzzer('start', 'fake-package1/fake-target1')
        fuzzer.require_stopped()
        self.set_running(fuzzer.package, fuzzer.executable)
        with self.assertRaises(SystemExit):
            fuzzer.require_stopped()
        self.assertLogged(
            'ERROR: fake-package1/fake-target1 is running and must be stopped first.',
        )
        self.stop_all()
        fuzzer.require_stopped()

    # Helper to test Fuzzer.start with different options and arguments. The
    # expected options and arguments to be passed to libFuzzer are given by
    # extra_args. If the helper is expected to raise an exception, leave
    # `expected` empty and have the caller handle the exception.
    def start_helper(self, extra_args, expected):
        fuzzer = self.create_fuzzer(
            'start', 'fake-package1/fake-target2', *extra_args)
        fuzzer.start()
        if expected:
            self.assertSsh('run', fuzzer.url(), *expected)

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
        with self.assertRaises(SystemExit):
            self.start_helper(['-option2=foo', '-option1'], None)

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
    def symbolize_helper(self, log_in, log_out, job_num=0, echo=False):
        fuzzer = self.create_fuzzer('start', 'fake-package1/fake-target2')
        cmd = [
            self.host.symbolizer_exec, '-llvm-symbolizer',
            self.host.llvm_symbolizer
        ]
        for build_id_dir in self.host.build_id_dirs:
            cmd += ['-build-id-dir', build_id_dir]
        self.set_outputs(
            cmd, [
                '[000001.234567][123][456][klog] INFO: Symbolized line 1',
                '[000001.234568][123][456][klog] INFO: Symbolized line 2',
                '[000001.234569][123][456][klog] INFO: Symbolized line 3', ''
            ])
        self.cli.mkdir(fuzzer._output)
        with self.cli.open('unsymbolized', 'w+') as unsymbolized:
            unsymbolized.write(log_in)
            unsymbolized.seek(0)
            fuzzer.symbolize_log(unsymbolized, job_num, echo=echo)
        # If log_in does not contain a mutation sequence, it will match log_out
        # and the symbolizer will not be invoked.
        if log_in != log_out:
            self.assertRan(*cmd)
        with self.cli.open(fuzzer._logfile(job_num)) as symbolized:
            self.assertEqual(symbolized.read(), log_out)

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

    def test_symbolize_log_with_job_num(self):
        self.symbolize_helper(
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
            job_num=13)

    def test_symbolize_log_with_echo(self):
        self.symbolize_helper(
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

        self.assertLogged(
            '',
            '==67890== ERROR: libFuzzer: deadly signal',
            'Symbolized line 1',
            'Symbolized line 2',
            'Symbolized line 3',
            'MS: 1 SomeMutation; base unit: foo',
        )

    def test_stop(self):
        # Stopping when stopped has no effect
        fuzzer = self.create_fuzzer('stop', 'fake-package1/fake-target2')
        self.assertFalse(fuzzer.is_running())
        fuzzer.stop()
        self.assertFalse(fuzzer.is_running())

        self.set_running(fuzzer.package, fuzzer.executable)
        pid = self.device.getpid(fuzzer.package, fuzzer.executable)
        fuzzer.stop()
        self.assertSsh('kill', str(pid))

    def test_repro(self):
        # Globs should work, but only if they match files
        fuzzer = self.create_fuzzer(
            'repro',
            'package1/target2',
            'crash-*',
            'oom-feedface',
        )
        with self.assertRaises(SystemExit):
            fuzzer.repro()
        self.assertEqual(
            fuzzer.device.host.cli.log, [
                'ERROR: No matching files: "crash-*".',
            ])

        # Valid
        self.cli.touch('crash-deadbeef')
        self.cli.touch('crash-deadfa11')
        self.cli.touch('oom-feedface')

        fuzzer.repro()
        self.assertSsh(
            'run', fuzzer.url(), '-artifact_prefix=data/',
            'data/crash-deadbeef', 'data/crash-deadfa11', 'data/oom-feedface')


if __name__ == '__main__':
    unittest.main()
