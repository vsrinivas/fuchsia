#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import unittest

import test_env
import lib.command as command
from test_case import TestCaseWithFuzzer


class CommandTest(TestCaseWithFuzzer):

    # Unit tests

    def test_list_fuzzers(self):
        # No match
        args = self.parse_args('list', 'no/match')
        command.list_fuzzers(args, self.factory)
        self.assertLogged('No matching fuzzers found for "no/match".')

        # Multiple matches
        args = self.parse_args('list', 'fake-package2')
        command.list_fuzzers(args, self.factory)
        self.assertLogged(
            'Found 3 matching fuzzers for "fake-package2":',
            '  fake-package2/an-extremely-verbose-target-name',
            '  fake-package2/fake-target1',
            '  fake-package2/fake-target11',
        )

        # Multiple matches, with fuzzer tests.
        args = self.parse_args('list', 'fake-package1')
        command.list_fuzzers(args, self.factory)
        self.assertLogged(
            'Found 2 matching fuzzer tests for "fake-package1":',
            '  fake-package1/fake-target4_test',
            '  fake-package1/fake-target5_test',
            '',
            'These tests correspond to fuzzers, but were not selected by the build arguments',
            'to be built with a fuzzer toolchain variant.',
            '',
            'To select them, you can use `fx set ... --fuzz-with <sanitizer>`.',
            'See https://fuchsia.dev/fuchsia-src/development/testing/fuzzing/build-a-fuzzer',
            'for additional details.',
            '',
            'Found 3 matching fuzzers for "fake-package1":',
            '  fake-package1/fake-target1',
            '  fake-package1/fake-target2',
            '  fake-package1/fake-target3',
        )

        # Exact match of a fuzzer
        args = self.parse_args('list', 'fake-package1/fake-target1')
        command.list_fuzzers(args, self.factory)
        self.assertLogged(
            'Found 1 matching fuzzer for "fake-package1/fake-target1":',
            '  fake-package1/fake-target1',
        )

        # Exact match of a fuzzer test
        args = self.parse_args('list', 'fake-package1/fake-target4')
        command.list_fuzzers(args, self.factory)
        self.assertLogged(
            'Found 1 matching fuzzer test for "fake-package1/fake-target4":',
            '  fake-package1/fake-target4_test', '',
            'This test corresponds to a fuzzer, but was not selected by the build arguments',
            'to be built with a fuzzer toolchain variant.', '',
            'To select them, you can use `fx set ... --fuzz-with <sanitizer>`.',
            'See https://fuchsia.dev/fuchsia-src/development/testing/fuzzing/build-a-fuzzer',
            'for additional details.', '',
            'No matching fuzzers found for "fake-package1/fake-target4".')

    def test_start_fuzzer(self):
        name = str(self.fuzzer)

        # In the foreground
        output = 'output'
        self.host.mkdir(output)
        args = self.parse_args('start', '-f', '-o', output, name)
        command.start_fuzzer(args, self.factory)
        self.assertLogged(
            'Starting {}.'.format(self.fuzzer),
            'Outputs will be written to: {}'.format(output),
        )

        # In the background
        args = self.parse_args('start', '-o', output, name)
        command.start_fuzzer(args, self.factory)
        self.assertLogged(
            'Starting {}.'.format(self.fuzzer),
            'Outputs will be written to: {}'.format(output),
            'Check status with "fx fuzz check {}".'.format(self.fuzzer),
            'Stop manually with "fx fuzz stop {}".'.format(self.fuzzer),
        )
        cmd = [
            'python',
            sys.argv[0],
            'start',
            '--monitor',
            '--output',
            output,
            name,
        ]
        self.assertRan(*cmd)

        args = self.parse_args('start', '-m', '-o', output, name)
        command.start_fuzzer(args, self.factory)
        self.assertLogged(
            '{} has stopped.'.format(self.fuzzer),
            'Output written to: {}.'.format(output),
        )

    def test_check_fuzzer(self):
        # No name, none running
        args = self.parse_args('check')
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'No fuzzers are running.',
            'Include \'name\' to check specific fuzzers.',
        )

        # Name provided, not installed
        fuzzer1 = self.create_fuzzer(
            'fake-package2/fake-target1', resolve=False)
        args = self.parse_args('check', 'fake-package2/fake-target1')
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'fake-package2/fake-target1: NOT INSTALLED',
            '',
        )

        # No name, some running
        fuzzer1 = self.create_fuzzer('fake-package1/fake-target1')
        fuzzer2 = self.create_fuzzer('fake-package1/fake-target2')
        fuzzer3 = self.create_fuzzer('fake-package1/fake-target3')
        self.set_running(fuzzer1.executable_url)
        self.set_running(fuzzer3.executable_url)
        args = self.parse_args('check')
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'fake-package1/fake-target1: RUNNING',
            '    Corpus size:  0 inputs / 0 bytes',
            '',
            'fake-package1/fake-target3: RUNNING',
            '    Corpus size:  0 inputs / 0 bytes',
            '',
        )

        # Name provided, running
        args = self.parse_args('check', 'fake-package1/fake-target3')
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'fake-package1/fake-target3: RUNNING',
            '    Corpus size:  0 inputs / 0 bytes',
            '',
        )

        # Name provided, not running
        args = self.parse_args('check', 'fake-package1/fake-target2')
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'fake-package1/fake-target2: STOPPED',
            '    Corpus size:  0 inputs / 0 bytes',
            '',
        )

        # Add some artifacts
        fuzzer = self.create_fuzzer('fake-package1/fake-target2')
        self.host.touch(os.path.join(fuzzer.output, 'crash-deadbeef'))
        self.host.touch(os.path.join(fuzzer.output, 'leak-feedface'))
        command.check_fuzzer(args, self.factory)
        self.assertLogged(
            'fake-package1/fake-target2: STOPPED',
            '    Corpus size:  0 inputs / 0 bytes',
            '    Artifacts:',
            '        {}/crash-deadbeef'.format(fuzzer.output),
            '        {}/leak-feedface'.format(fuzzer.output),
            '',
        )

    def test_stop_fuzzer(self):
        # Not running
        args = self.parse_args('stop', str(self.fuzzer))
        command.stop_fuzzer(args, self.factory)
        self.assertLogged('{} is already stopped.'.format(self.fuzzer))

        # Running
        self.set_running(self.fuzzer.executable_url)
        args = self.parse_args('stop', str(self.fuzzer))
        command.stop_fuzzer(args, self.factory)
        self.assertLogged('Stopping {}.'.format(self.fuzzer))

    def test_repro_units(self):
        unit = 'crash-deadbeef'
        self.host.touch(unit)
        args = self.parse_args('repro', str(self.fuzzer), unit)
        command.repro_units(args, self.factory)

    def test_analyze_fuzzer(self):
        args = self.parse_args('analyze', '-l', str(self.fuzzer))
        command.analyze_fuzzer(args, self.factory)

        # We shouldn't have copied anything
        self.assertFalse([cmd for cmd in self.host.processes if 'gs' in cmd])

        # Invalid corpus
        corpus1 = 'corpus1'
        corpus2 = 'corpus2'
        local_dict = 'local_dict'
        args = self.parse_args(
            'analyze', '-c', corpus1, '-c', corpus2, '-d', local_dict,
            '{}/{}'.format(self.fuzzer.package, self.fuzzer.executable))
        self.assertError(
            lambda: command.analyze_fuzzer(args, self.factory),
            'No such directory: {}'.format(corpus1))

        self.host.mkdir(corpus1)
        foo = os.path.join(corpus1, 'foo')
        bar = os.path.join(corpus1, 'bar')
        self.host.touch(foo)
        self.host.touch(bar)
        self.assertError(
            lambda: command.analyze_fuzzer(args, self.factory),
            'No such directory: {}'.format(corpus2))

        # Invalid dictionary
        self.host.mkdir(corpus2)
        baz = os.path.join(corpus2, 'baz')
        self.host.touch(baz)
        self.assertError(
            lambda: command.analyze_fuzzer(args, self.factory),
            'No such file: {}'.format(local_dict))

        self.host.touch(local_dict)
        with self.host.temp_dir() as temp_dir:
            # Make it appear as if something was retrieved from GCS.
            qux = os.path.join(temp_dir.pathname, 'qux')
            self.host.touch(qux)
        command.analyze_fuzzer(args, self.factory)
        gcs_url = 'gs://corpus.internal.clusterfuzz.com/libFuzzer/fuchsia_{}-{}'.format(
            self.fuzzer.package, self.fuzzer.executable)
        with self.host.temp_dir() as temp_dir:
            cmd = ['gsutil', '-m', 'cp', gcs_url + '/*', temp_dir.pathname]
            self.assertRan(*cmd)

        abspath = self.ns.data_abspath(self.corpus.nspaths[0])
        self.assertScpTo(bar, foo, abspath)
        self.assertScpTo(baz, abspath)
        self.assertEqual(
            self.dictionary.nspath, self.fuzzer.ns.data(local_dict))

    def test_update_corpus(self):
        # This test relies on data from "test/data/v2.fuzzers.json".
        # Fuzzer without corpus directory specified in its metadata.
        args = self.parse_args('update', '1/1')
        fuzzer = self.create_fuzzer('1/1')
        self.host.cwd = self.buildenv.abspath('//current_dir')
        self.set_input('foo')
        self.assertError(
            lambda: command.update_corpus(args, self.factory),
            'No such directory: /fuchsia_dir/current_dir/foo')
        # This was logged before the error
        self.assertLogged('No corpus set for {}.'.format(str(fuzzer)))

        # Fuzzer with empty corpus
        args = self.parse_args('update', '1/2')
        fuzzer = self.create_fuzzer('1/2')
        corpus_dir = self.buildenv.abspath(fuzzer.corpus.srcdir)
        build_gn = self.buildenv.abspath(corpus_dir, 'BUILD.gn')
        self.host.mkdir(corpus_dir)

        self.assertFalse(self.host.isfile(build_gn))
        command.update_corpus(args, self.factory)
        self.assertLogged(
            'Empty corpus added.', '',
            '{}/BUILD.gn updated.'.format(fuzzer.corpus.srcdir))
        self.assertTrue(self.host.isfile(build_gn))

        # Fuzzer with explicit GN file.
        build_gn = '//src/fake/new.gn'
        args = self.parse_args('update', '-o', build_gn, '1/3')
        fuzzer = self.create_fuzzer('1/3')
        corpus_dir = self.buildenv.abspath(fuzzer.corpus.srcdir)
        build_gn = self.buildenv.abspath(build_gn)
        self.host.mkdir(corpus_dir)
        self.host.touch(self.buildenv.abspath(corpus_dir, 'foo'))

        self.assertFalse(self.host.isfile(build_gn))
        command.update_corpus(args, self.factory)
        self.assertLogged(
            'Added:', '  package1/target3-corpus/foo', '',
            '//{} updated.'.format(
                os.path.relpath(build_gn, self.buildenv.fuchsia_dir)))
        self.assertTrue(self.host.isfile(build_gn))

        # Fuzzer not currently built as fuzzer
        args = self.parse_args('update', '1/5')
        fuzzer = self.create_fuzzer('1/5', include_tests=True)
        corpus_dir = self.buildenv.abspath(fuzzer.corpus.srcdir)
        self.host.mkdir(corpus_dir)
        self.host.touch(self.buildenv.abspath(corpus_dir, 'bar'))
        command.update_corpus(args, self.factory)
        self.assertLogged(
            'Added:', '  bar', '',
            '{}/BUILD.gn updated.'.format(fuzzer.corpus.srcdir))

    def test_measure_coverage(self):
        fuzzer = self.create_fuzzer('1/1')
        output_dir = 'my/dir'
        self.host.mkdir(output_dir)
        args = self.parse_args('coverage', '-o', output_dir, str(fuzzer))
        self.assertError(
            lambda: command.measure_coverage(args, self.factory),
            'FUCHSIA_SSH_KEY not set, by default this should be the private key in ~/.ssh/'
        )


if __name__ == '__main__':
    unittest.main()
