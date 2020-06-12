#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

import test_env
from test_case import TestCaseWithFuzzer


class FuzzerTest(TestCaseWithFuzzer):

    def test_list_artifacts(self):
        fuzzer = self.create_fuzzer('check', 'fake-package1/fake-target1')
        artifacts = ['crash-deadbeef', 'leak-deadfa11', 'oom-feedface']
        artifacts = [os.path.join(fuzzer.output, a) for a in artifacts]
        for artifact in artifacts:
            self.host.touch(artifact)
        self.assertEqual(fuzzer.list_artifacts(), artifacts)

    def test_is_running(self):
        fuzzer1 = self.create_fuzzer('check', 'fake-package1/fake-target1')
        fuzzer2 = self.create_fuzzer('check', 'fake-package1/fake-target2')

        self.assertFalse(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

        self.set_running(fuzzer1.package, fuzzer1.executable, duration=5)
        self.assertTrue(fuzzer1.is_running())
        self.assertFalse(fuzzer2.is_running())

        self.set_running(fuzzer2.package, fuzzer2.executable, duration=5)
        self.assertTrue(fuzzer1.is_running())
        self.assertTrue(fuzzer2.is_running())

        # Results are cached until refresh
        self.host.sleep(5)
        self.assertTrue(fuzzer1.is_running())
        self.assertTrue(fuzzer2.is_running())

        self.assertFalse(fuzzer1.is_running(refresh=True))
        self.assertFalse(fuzzer2.is_running(refresh=True))

    def test_require_stopped(self):
        fuzzer = self.create_fuzzer('start', 'fake-package1/fake-target1')
        fuzzer.require_stopped()

        self.set_running(fuzzer.package, fuzzer.executable, duration=10)
        self.assertError(
            lambda: fuzzer.require_stopped(),
            'fake-package1/fake-target1 is running and must be stopped first.')
        self.host.sleep(10)
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
                'data/corpus',
            ])

    def test_start_already_running(self):
        fuzzer = self.create_fuzzer('start', 'fake-package1/fake-target1')
        self.set_running(fuzzer.package, fuzzer.executable)
        self.assertError(
            lambda: fuzzer.start(),
            'fake-package1/fake-target1 is running and must be stopped first.')

    def test_start_foreground(self):
        self.start_helper(
            ['--foreground'], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                'data/corpus',
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
                'data/corpus',
            ])

    def test_start_with_options(self):
        self.start_helper(
            ['-option2=foo', '-option1=\'bar baz\''], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                '-jobs=1',
                '-option1=\'bar baz\'',
                '-option2=foo',
                'data/corpus',
            ])

    def test_start_with_bad_option(self):
        self.assertError(
            lambda: self.start_helper(['-option2=foo', '-option1'], None),
            'Unrecognized option: -option1', 'Try "fx fuzz help".')

    def test_start_with_passthrough(self):
        self.start_helper(
            ['-option2=foo', '--', '-option1=bar'], [
                '-artifact_prefix=data/',
                '-dict=pkg/data/fake-target2/dictionary',
                '-jobs=1',
                '-option2=foo',
                'data/corpus',
                '--',
                '-option1=bar',
            ])

    def test_start_failure(self):
        fuzzer = self.create_fuzzer('start', 'fake-package1/fake-target2')

        # Make the fuzzer fail after 20 seconds.
        cmd = [
            'run',
            fuzzer.url(),
            '-artifact_prefix=data/',
            '-dict=pkg/data/fake-target2/dictionary',
            '-jobs=1',
            'data/corpus',
        ]
        process = self.get_process(cmd, ssh=True)
        process.duration = 20
        process.succeeds = False

        # Start the fuzzer
        self.assertError(
            lambda: fuzzer.start(),
            'fake-package1/fake-target2 failed to start.')
        self.assertSsh(*cmd)
        self.assertEqual(self.host.elapsed, 20)

    def test_start_slow_resolve(self):
        fuzzer = self.create_fuzzer('start', 'fake-package1/fake-target2')

        # Make the log file appear after 15 "seconds".
        cmd = ['ls', '-l', self.data_abspath('fuzz-*.log')]
        process = self.get_process(cmd, ssh=True)
        process.schedule(
            start=15, output='-rw-r--r-- 1 0 0 0 Dec 25 00:00 fuzz-0.log')

        # Make the fuzzer finish after 20 seconds.
        cmd = [
            'run',
            fuzzer.url(),
            '-artifact_prefix=data/',
            '-dict=pkg/data/fake-target2/dictionary',
            '-jobs=1',
            'data/corpus',
        ]
        process = self.get_process(cmd, ssh=True)
        process.duration = 20

        # Start the fuzzer
        fuzzer.start()
        self.assertSsh(*cmd)
        self.assertEqual(self.host.elapsed, 15)

    # Helper to test Fuzzer.symbolize with different logs.
    def symbolize_helper(self, log_in, log_out, job_num=0, echo=False):
        fuzzer = self.create_fuzzer('start', 'fake-package1/fake-target2')
        cmd = [
            self.buildenv.symbolizer_exec, '-llvm-symbolizer',
            self.buildenv.llvm_symbolizer
        ]
        for build_id_dir in self.buildenv.build_id_dirs:
            cmd += ['-build-id-dir', build_id_dir]
        self.set_outputs(
            cmd, [
                '[000001.234567][123][456][klog] INFO: Symbolized line 1',
                '[000001.234568][123][456][klog] INFO: Symbolized line 2',
                '[000001.234569][123][456][klog] INFO: Symbolized line 3',
            ])
        self.host.mkdir(fuzzer._output)
        with self.host.open('unsymbolized', 'w+') as unsymbolized:
            unsymbolized.write(log_in)
            unsymbolized.seek(0)
            fuzzer.symbolize_log(unsymbolized, job_num, echo=echo)
        # If log_in does not contain a mutation sequence, it will match log_out
        # and the symbolizer will not be invoked.
        if log_in != log_out:
            self.assertRan(*cmd)
        with self.host.open(fuzzer.logfile(job_num)) as symbolized:
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

    def test_monitor(self):
        fuzzer = self.create_fuzzer('start', 'fake-package1/fake-target2')
        self.set_running(fuzzer.package, fuzzer.executable, duration=15)

        # Make the file that scp grabs
        self.host.mkdir(fuzzer.output)

        logname = os.path.join(fuzzer.output, 'fuzz-0.log')
        with self.host.open(logname, 'w') as log:
            log.write('==67890== libFuzzer: deadly signal\n')
            log.write('MS: 1 SomeMutation; base unit: foo\n')

        # Monitor the fuzzer until it exits
        fuzzer.monitor()
        self.assertGreaterEqual(self.host.elapsed, 15)

        # Verify we grabbed the logs and symbolized them.
        self.assertScpFrom(self.data_abspath('fuzz-*.log'), fuzzer.output)
        cmd = ['rm', '-f', self.data_abspath('fuzz-*.log')]
        self.assertSsh(*cmd)

        cmd = [
            self.buildenv.symbolizer_exec, '-llvm-symbolizer',
            self.buildenv.llvm_symbolizer
        ]
        for build_id_dir in self.buildenv.build_id_dirs:
            cmd += ['-build-id-dir', build_id_dir]
        self.assertRan(*cmd)

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

        # Invalid units
        self.assertError(
            lambda: fuzzer.repro(),
            'No matching files: "crash-* oom-feedface".')

        # Valid units, but already running
        self.host.touch('crash-deadbeef')
        self.host.touch('crash-deadfa11')
        self.host.touch('oom-feedface')
        self.set_running(fuzzer.package, fuzzer.executable, duration=60)
        self.assertError(
            lambda: fuzzer.repro(),
            'fake-package1/fake-target2 is running and must be stopped first.')
        self.host.sleep(60)

        #  Valid
        fuzzer.repro()
        self.assertSsh(
            'run',
            fuzzer.url(),
            '-artifact_prefix=data/',
            'data/crash-deadbeef',
            'data/crash-deadfa11',
            'data/oom-feedface',
        )

    def test_analyze(self):
        fuzzer = self.create_fuzzer('analyze', 'fake-package1/fake-target2')

        self.set_running(fuzzer.package, fuzzer.executable, duration=10)
        with self.assertRaises(SystemExit):
            fuzzer.analyze()
        self.host.sleep(10)

        # Make the log file appear right away
        cmd = [
            'ls', '-l',
            self.fuzzer.ns.abspath(self.fuzzer.ns.data('fuzz-*.log'))
        ]
        self.set_outputs(
            cmd, ['-rw-r--r-- 1 0 0 0 Dec 25 00:00 fuzz-0.log'], ssh=True)

        # Make the fuzzer run for 60 "seconds".
        cmd = [
            'run',
            fuzzer.url(),
            '-artifact_prefix=data/',
            '-dict=pkg/data/fake-target2/dictionary',
            '-jobs=1',
            '-max_total_time=60',
            '-print_coverage=1',
            'data/corpus',
            'pkg/data/fake-target2/corpus',
        ]
        process = self.get_process(cmd, ssh=True)
        process.duration = 60

        fuzzer.analyze()
        self.assertSsh(*cmd)

        # Round to the nearest microsecond
        self.assertEqual(round(self.host.elapsed, 6), 70)


if __name__ == '__main__':
    unittest.main()
