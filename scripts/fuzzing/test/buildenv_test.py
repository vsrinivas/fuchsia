#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import unittest

import test_env
from lib.buildenv import BuildEnv
from test_case import TestCaseWithFactory


class BuildEnvTest(TestCaseWithFactory):

    def test_fuchsia_dir(self):
        self.assertError(
            lambda: BuildEnv(self.factory), 'FUCHSIA_DIR not set.',
            'Have you sourced "scripts/fx-env.sh"?')

    def test_configure(self):
        fuchsia_dir = os.path.abspath('/test_configure')
        self.host.mkdir(fuchsia_dir)
        self.host.setenv('FUCHSIA_DIR', fuchsia_dir)

        # Fails due to missing paths
        buildenv = BuildEnv(self.factory)
        build_dir = os.path.join(fuchsia_dir, 'build_dir')
        self.host.mkdir(build_dir)

        # No $FUCHSIA_DIR/out/default/host_x64/symbolize
        symbolizer_exec = os.path.join(build_dir, 'host_x64', 'symbolize')
        self.assertError(
            lambda: buildenv.configure(build_dir),
            'Invalid symbolizer executable: {}'.format(symbolizer_exec))
        self.host.touch(symbolizer_exec)

        # No $FUCHSIA_DIR/prebuild/third_party/clang/bin/llvm-symbolizer
        clang_dir = os.path.join(
            fuchsia_dir, 'prebuilt', 'third_party', 'clang', self.host.platform)
        llvm_symbolizer = os.path.join(clang_dir, 'bin', 'llvm-symbolizer')
        self.assertError(
            lambda: buildenv.configure(build_dir),
            'Invalid LLVM symbolizer: {}'.format(llvm_symbolizer))
        self.host.touch(llvm_symbolizer)

        # No $FUCHSIA_DIR/.../.build-id
        build_id_dirs = [
            os.path.join(clang_dir, 'lib', 'debug', '.build-id'),
            os.path.join(build_dir, '.build-id'),
            os.path.join(build_dir + '.zircon', '.build-id'),
        ]
        for build_id_dir in build_id_dirs:
            srcpath = buildenv.srcpath(build_id_dir)
            self.assertError(
                lambda: buildenv.configure(build_dir),
                'Invalid build ID directory: {}'.format(srcpath),
            )
            self.host.mkdir(build_id_dir)

        # No $FUCHSIA_DIR/prebuild/third_party/clang/bin/llvm-cov
        llvm_cov = os.path.join(clang_dir, 'bin', 'llvm-cov')
        self.assertError(
            lambda: buildenv.configure(build_dir),
            'Invalid LLVM cov: {}'.format(llvm_cov))
        self.host.touch(llvm_cov)

        # No $FUCHSIA_DIR/prebuild/third_party/clang/bin/llvm-profdata
        llvm_profdata = os.path.join(clang_dir, 'bin', 'llvm-profdata')
        self.assertError(
            lambda: buildenv.configure(build_dir),
            'Invalid LLVM profdata: {}'.format(llvm_profdata))
        self.host.touch(llvm_profdata)

        buildenv.configure(build_dir)
        clang_dir = '//prebuilt/third_party/clang/' + self.host.platform

        self.assertEqual(buildenv.build_dir, buildenv.abspath(build_dir))
        self.assertEqual(
            buildenv.symbolizer_exec,
            buildenv.abspath(build_dir + '/host_x64/symbolize'))
        self.assertEqual(
            buildenv.llvm_symbolizer,
            buildenv.abspath(clang_dir + '/bin/llvm-symbolizer'))
        self.assertEqual(
            buildenv.build_id_dirs, [
                buildenv.abspath(clang_dir + '/lib/debug/.build-id'),
                buildenv.abspath(build_dir + '/.build-id'),
                buildenv.abspath(build_dir + '.zircon/.build-id'),
            ])

    # Unit tests

    def test_read_fuzzers(self):
        fuchsia_dir = 'test_read_fuzzers'
        self.host.mkdir(fuchsia_dir)
        self.host.setenv('FUCHSIA_DIR', fuchsia_dir)
        buildenv = BuildEnv(self.factory)

        expected_fuzzers = [
            'fake-package1/fake-target1',
            'fake-package1/fake-target2',
            'fake-package1/fake-target3',
            'fake-package2/an-extremely-verbose-target-name',
            'fake-package2/fake-target1',
            'fake-package2/fake-target11',
        ]
        expected_fuzzer_tests = [
            'fake-package1/fake-target4',
            'fake-package1/fake-target5',
        ]

        # v1 doesn't include fuzzer tests
        golden = 'data/v1.fuzzers.json'
        self.host.add_golden(golden)
        buildenv.read_fuzzers(golden)
        fuzzers = [str(fuzzer) for fuzzer in buildenv.fuzzers()]
        self.assertEqual(fuzzers, expected_fuzzers)
        self.assertFalse(buildenv.fuzzer_tests())

        # v2 can select fuzzers...
        golden = 'data/v2.fuzzers.json'
        self.host.add_golden(golden)
        buildenv.read_fuzzers(golden)

        fuzzers = [str(fuzzer) for fuzzer in buildenv.fuzzers()]
        self.assertEqual(fuzzers, expected_fuzzers)

        # ...or fuzzer tests...
        fuzzer_tests = [str(fuzzer) for fuzzer in buildenv.fuzzer_tests()]
        self.assertEqual(fuzzer_tests, expected_fuzzer_tests)

        # ...or both!
        fuzzers = [
            str(fuzzer) for fuzzer in buildenv.fuzzers(include_tests=True)
        ]
        self.assertEqual(
            fuzzers, sorted(expected_fuzzers + expected_fuzzer_tests))

    def test_fuzzers(self):
        self.assertEqual(len(self.buildenv.fuzzers('/')), 6)
        self.assertEqual(len(self.buildenv.fuzzers('fake')), 6)
        self.assertEqual(len(self.buildenv.fuzzers('package1')), 3)
        self.assertEqual(len(self.buildenv.fuzzers('target1')), 3)
        self.assertEqual(len(self.buildenv.fuzzers('package2/target1')), 2)
        self.assertEqual(
            len(self.buildenv.fuzzers('fake-package2/fake-target1')), 1)
        self.assertEqual(len(self.buildenv.fuzzers('1/2')), 1)
        self.assertEqual(len(self.buildenv.fuzzers('target4')), 0)
        with self.assertRaises(ValueError):
            self.buildenv.fuzzers('a/b/c')

    def test_fuzzer_tests(self):
        self.assertEqual(len(self.buildenv.fuzzer_tests('/')), 2)
        self.assertEqual(len(self.buildenv.fuzzer_tests('fake')), 2)
        self.assertEqual(len(self.buildenv.fuzzer_tests('package1')), 2)
        self.assertEqual(len(self.buildenv.fuzzer_tests('target1')), 0)
        self.assertEqual(len(self.buildenv.fuzzer_tests('package2/target1')), 0)
        self.assertEqual(
            len(self.buildenv.fuzzer_tests('fake-package1/fake-target5')), 1)
        self.assertEqual(len(self.buildenv.fuzzer_tests('1/5')), 1)
        self.assertEqual(len(self.buildenv.fuzzer_tests('target4')), 1)
        with self.assertRaises(ValueError):
            self.buildenv.fuzzer_tests('a/b/c')

    def test_abspath(self):
        self.host.cwd = os.path.join(self.buildenv.fuchsia_dir, 'foo')
        self.assertEqual(
            self.buildenv.abspath('//bar/baz'),
            os.path.join(self.buildenv.fuchsia_dir, 'bar', 'baz'))
        self.assertEqual(
            self.buildenv.abspath('/bar/baz'), os.path.abspath('/bar/baz'))
        self.assertEqual(
            self.buildenv.abspath('baz'),
            os.path.join(self.buildenv.fuchsia_dir, 'foo/baz'))

    def test_srcpath(self):
        self.assertEqual(self.buildenv.srcpath('//foo/bar'), '//foo/bar')
        self.assertEqual(self.buildenv.srcpath('//foo/bar:baz'), '//foo/bar')
        self.host.cwd = os.path.join(self.buildenv.fuchsia_dir, 'foo')
        self.assertEqual(self.buildenv.srcpath('bar'), '//foo/bar')
        self.assertEqual(self.buildenv.srcpath('bar:baz'), '//foo/bar')
        self.assertError(
            lambda: self.buildenv.srcpath('/foo/bar'),
            '/foo/bar is not a path in the source tree.')
        self.assertError(
            lambda: self.buildenv.srcpath('/foo/bar:baz'),
            '/foo/bar is not a path in the source tree.')
        self.host.cwd = '/qux'
        self.assertError(
            lambda: self.buildenv.srcpath('bar'),
            '/qux/bar is not a path in the source tree.')
        self.assertError(
            lambda: self.buildenv.srcpath('bar:baz'),
            '/qux/bar is not a path in the source tree.')

    def test_find_device(self):
        device_name = 'test_find_device'
        addrs = ['::1', '::2']

        cmd = [
            self.buildenv.abspath('//.jiri_root/bin/fx'),
            'device-finder',
            'resolve',
            '-device-limit',
            '1',
            device_name,
        ]
        self.set_outputs(cmd, addrs[:1])
        self.assertEqual(self.buildenv.find_device(device_name), addrs[0])

        # No results from 'fx device-finder list'
        self.assertError(
            lambda: self.buildenv.find_device(None), 'Unable to find device.',
            'Try "fx set-device".')

        # Multiple results from `fx device-finder list`
        cmd = [
            self.buildenv.abspath('//.jiri_root/bin/fx'), 'device-finder',
            'list'
        ]
        self.set_outputs(cmd, addrs)
        self.assertError(
            lambda: self.buildenv.find_device(None), 'Multiple devices found.',
            'Try "fx set-device".')

        # Reset output
        self.set_outputs(cmd, addrs[:1])
        self.assertEqual(self.buildenv.find_device(None), addrs[0])

    def test_symbolize(self):
        stacktrace = [
            'a line',
            'another line',
            'yet another line',
        ]
        cmd = self.symbolize_cmd()
        self.set_outputs(
            cmd, [
                '[000001.234567][123][456][klog] INFO: Symbolized line 1',
                '[000001.234568][123][456][klog] INFO: Symbolized line 2',
                '[000001.234569][123][456][klog] INFO: Symbolized line 3'
            ])
        symbolized = self.buildenv.symbolize('\n'.join(stacktrace))

        self.assertRan(*cmd)
        process = self.get_process(cmd)
        self.assertEqual(process.inputs, stacktrace)
        self.assertEqual(
            symbolized.strip().split('\n'), [
                'Symbolized line 1',
                'Symbolized line 2',
                'Symbolized line 3',
            ])

    def test_testsharder(self):
        # Prerequisites
        fuzzer = self.buildenv.fuzzers()[0]

        # Capture the testsharder command
        testsharder_out_file = os.path.join(
            fuzzer.output, 'testsharder_out.json')
        cmd = [os.path.join(self.buildenv.build_dir, 'host_x64', 'testsharder')] \
            + ['-build-dir', self.buildenv.build_dir] \
            + ['-max-shards-per-env', '1'] \
            + ['-output-file', testsharder_out_file]
        self.set_outputs(cmd, [], returncode=0, reset=True)

        # Write a sharder out with > 1 shards
        with self.host.open(testsharder_out_file, 'w') as f:
            json.dump(
                [
                    {
                        'name': 'AEMU-unittest',
                        'tests': [],
                    }, {
                        'name': 'AEMU-unittest-2',
                        'tests': [],
                    }
                ], f)
        self.assertError(
            lambda: self.buildenv.testsharder(
                fuzzer.executable_url, fuzzer.output),
            'Expected a single shard, but got 2.')

        # Write a sharder out without any AEMU shards
        with self.host.open(testsharder_out_file, 'w') as f:
            json.dump([{
                'name': 'not-AEMU',
                'tests': [],
            }], f)
        self.assertError(
            lambda: self.buildenv.testsharder(
                fuzzer.executable_url, fuzzer.output),
            'Unable to find any tests for AEMU shards.')

        # Write a sharder out with tests that does not have any matching tests.
        tests = [{
            'name': 'not-a-url',
            'meta': 'baz',
            'meta1': 'bam',
        }]
        with self.host.open(testsharder_out_file, 'w') as f:
            json.dump([{
                'name': 'AEMU-unittest',
                'tests': tests,
            }], f)
        self.assertError(
            lambda: self.buildenv.testsharder(
                fuzzer.executable_url, fuzzer.output),
            'Found no matching tests to run.')

        # Write a good sharder out
        tests = [
            {
                'name': fuzzer.executable_url,
                'meta': 'foo',
                'meta1': 'bar',
            }, {
                'name': 'not-a-url',
                'meta': 'baz',
                'meta1': 'bam',
            }
        ]
        with self.host.open(testsharder_out_file, 'w') as f:
            json.dump([{'name': 'AEMU-unittest', 'tests': tests}], f)

        # Assert that the output is only the fuzzer test descriptor
        out_file = self.buildenv.testsharder(
            fuzzer.executable_url, fuzzer.output)
        self.assertRan(*cmd)
        with self.host.open(out_file) as f:
            self.assertEqual(json.loads(f.read()), [tests[0]])

    def test_testrunner(self):
        # Prerequisites
        fuzzer = self.buildenv.fuzzers()[0]
        shard_file = os.path.join(fuzzer.output, 'fake_shard_tests.json')

        # Assert that a missing shard file causes an error
        self.assertError(
            lambda: self.buildenv.testrunner(
                shard_file, fuzzer.output, self.device),
            'Unable to find sharded test file at {}.'.format(shard_file))

        # Create the missing shard file
        self.host.touch(shard_file)

        # Capture testrunner command
        runner_out_dir = os.path.join(fuzzer.output, 'testrunner_out/')
        cmd = self.infra_testrunner_cmd(runner_out_dir, shard_file)

        # Set an output that does not contain any valid pids lines
        self.set_outputs(
            cmd, ['[1.2][3] not a valid pid line'], returncode=0, reset=True)
        self.assertError(
            lambda: self.buildenv.testrunner(
                shard_file, fuzzer.output, self.device),
            'Unable to find a matching test fuzzer pid.')

        # Set an output that contains a valid pid line
        fake_pid = 101
        self.set_outputs(
            cmd, [
                '[123.456][{}][102][foo.cmx] INFO: [fuzzer_test.cc(35)] Fuzzer built as test: foo/bar'
                .format(fake_pid)
            ],
            returncode=0,
            reset=True)

        # Capture device dumplog
        cmd_2 = [
            'log_listener', '--dump_logs', 'yes', '--pretty', 'no', '--pid',
            str(fake_pid)
        ]
        raw_log_dump = [
            'Cupcake ipsum dolor sit amet cake pastry sesame snaps.',
            'Jujubes chocolate cake lemon drops cotton candy lemon drops.',
            'Oat cake souffle sugar plum pastry biscuit muffin.'
        ]
        self.set_outputs(
            cmd_2, raw_log_dump, returncode=0, ssh=True, reset=True)

        # Assert that we pulled the correct logs out.
        ret_runner_out_dir, log_dump_out = self.buildenv.testrunner(
            shard_file, fuzzer.output, self.device)
        self.assertRan(*cmd)
        self.assertSsh(*cmd_2)
        with self.host.open(log_dump_out) as f:
            # File format is a joined log dump with a trailing newline
            expected_log_dump = raw_log_dump[:]
            expected_log_dump.append('')
            self.assertEqual(f.read(), '\n'.join(expected_log_dump))
        self.assertEqual(ret_runner_out_dir, runner_out_dir)

    def test_covargs(self):
        #Prerequisites
        fuzzer = self.buildenv.fuzzers()[0]
        testrunner_dir = os.path.join(fuzzer.output, 'testrunner_out')
        summary_json_file = os.path.join(testrunner_dir, 'summary.json')
        symbolize_file = os.path.join(fuzzer.output, 'fake_symbolize.json')

        # Assert that a missing summary.json file causes an error
        self.assertError(
            lambda: self.buildenv.covargs(
                testrunner_dir, symbolize_file, fuzzer.output),
            'Unable to find summary.json file at {}.'.format(summary_json_file))

        # Create summary json file
        self.host.touch(summary_json_file)

        # Assert that a missing symbolize output file causes an error
        self.assertError(
            lambda: self.buildenv.covargs(
                testrunner_dir, symbolize_file, fuzzer.output),
            'Unable to find symbolize file at {}.'.format(symbolize_file))

        # Create symbolize output file
        self.host.touch(symbolize_file)

        # Capture covargs command
        coverage_out_dir = os.path.join(fuzzer.output, 'covargs_out')
        cmd = [os.path.join(self.buildenv.build_dir, 'host_x64', 'covargs')] \
            + ['-llvm-cov', self.buildenv.llvm_cov] \
            + ['-llvm-profdata', self.buildenv.llvm_profdata] \
            + ['-summary', summary_json_file] \
            + ['-symbolize-dump', symbolize_file] \
            + ['-output-dir', coverage_out_dir]
        for build_id_dir in self.buildenv.build_id_dirs:
            cmd += ['-build-id-dir', build_id_dir]
        self.set_outputs(cmd, [], returncode=0, reset=True)

        ret_coverage_out_dir = self.buildenv.covargs(
            testrunner_dir, symbolize_file, fuzzer.output)
        self.assertRan(*cmd)
        self.assertEqual(ret_coverage_out_dir, coverage_out_dir)


if __name__ == '__main__':
    unittest.main()
