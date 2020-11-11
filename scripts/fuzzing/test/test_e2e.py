#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import glob
import os
import shutil
import struct
import sys
import tempfile
import unittest

import test_env
import lib.command as command
from lib.host import Host
from lib.factory import Factory
from test_case import TestCaseWithIO


# Override test loading so we can run a different set of tests depending on
# whether a fuzzer was specified as an argument
def load_tests(loader, standard_tests, pattern):
    if Factory().parser.parse_args().name:
        test_class = IntegrationTestSingle
    else:
        test_class = IntegrationTestFull

    return unittest.TestLoader().loadTestsFromTestCase(test_class)


class IntegrationTest(TestCaseWithIO):

    def assertNoErrors(self):
        """Convenience method to reset stdout and assert stderr is empty."""
        self.assertOut([], n=0)
        self.assertErr([])

    def setUp(self):
        super(IntegrationTest, self).setUp()

        # Set up hermetic environment.
        self.host = Host()
        self.host.fd_out = self._stdout
        self.host.fd_err = self._stderr

        self.factory = Factory(host=self.host)
        self.temp_dir = tempfile.mkdtemp()
        self.parser = self.factory.parser

    def tearDown(self):
        super(IntegrationTest, self).tearDown()

        shutil.rmtree(self.temp_dir)


class IntegrationTestFull(IntegrationTest):
    """Use several example fuzzers to test a wide variety of operations in
    more detail.  While more sensitive to changes in, e.g., libFuzzer output
    and example fuzzer implementations, this test provides the most thorough
    validation of standard workflows end-to-end. Requires //examples/fuzzers
    to have been built."""

    def test_basic_crash(self):
        cmd = self.parser.parse_args(
            [
                'start', '-f', '-o', self.temp_dir,
                'example-fuzzers/crash_fuzzer'
            ])
        cmd.command(cmd, self.factory)
        self.assertOutContains(
            'INFO: libFuzzer starting', 'ERROR: libFuzzer: deadly signal',
            'examples/fuzzers/cpp/crash.cc',
            'artifact_prefix=\'data/\'; Test unit written to data/crash-')
        artifacts = glob.glob(os.path.join(self.temp_dir, 'crash-*'))
        self.assertEqual(len(artifacts), 1)
        with self.host.open(artifacts[0], 'rb') as f:
            self.assertEqual(f.read(3), 'HI!')

    def test_start_stop(self):
        # This test covers interactions with on-device processes
        fuzzer = 'example-fuzzers/noop_fuzzer'

        # Reset to known state
        cmd = self.parser.parse_args(['stop', fuzzer])
        cmd.command(cmd, self.factory)

        cmd = self.parser.parse_args(['start', '-o', self.temp_dir, fuzzer])
        cmd.command(cmd, self.factory)
        self.assertOutContains('Starting {}'.format(fuzzer))

        cmd = self.parser.parse_args(['check', fuzzer])
        cmd.command(cmd, self.factory)
        self.assertOutContains('{}: RUNNING'.format(fuzzer))

        cmd = self.parser.parse_args(['stop', fuzzer])
        cmd.command(cmd, self.factory)
        self.assertOutContains('Stopping {}'.format(fuzzer))

        cmd = self.parser.parse_args(['check', fuzzer])
        cmd.command(cmd, self.factory)
        self.assertOutContains('{}: STOPPED'.format(fuzzer))

    def test_repro_asan(self):
        testfile = os.path.join(self.temp_dir, "overflow_input")

        # This will cause overflow_fuzzer to allocate 2 bytes and attempt to
        # write 4 bytes into it
        with self.host.open(testfile, 'wb') as f:
            f.write(struct.pack("<Q", 2) + "AAAA")

        cmd = self.parser.parse_args(
            ['repro', 'example-fuzzers/overflow_fuzzer', testfile])
        cmd.command(cmd, self.factory)
        self.assertOutContains(
            'INFO: libFuzzer starting',
            'ERROR: AddressSanitizer: heap-buffer-overflow',
            'examples/fuzzers/cpp/overflow.cc', 'ABORTING')

    def test_seed_corpus(self):
        # Here we pass a timeout because this example isn't designed to crash
        # immediately
        cmd = self.parser.parse_args(
            [
                'start', '-f', '-o', self.temp_dir,
                'example-fuzzers/corpus_fuzzer', '-max_total_time=1'
            ])
        self.factory.create_fuzzer(cmd).corpus.reset()
        cmd.command(cmd, self.factory)
        self.assertOutContains(
            'INFO: libFuzzer starting', '0 files found in data/corpus',
            '5 files found in pkg/data/examples/fuzzers/cpp/example-corpus',
            'INFO: seed corpus: files: 5')

    def test_live_corpus(self):
        corpus = {"element1": "wee", "element2": "large, relatively"}
        corpus_dir = os.path.join(self.temp_dir, "e2e-corpus")
        self.host.mkdir(corpus_dir)
        for name, contents in corpus.items():
            with self.host.open(os.path.join(corpus_dir, name), 'w') as f:
                f.write(contents)

        cmd = self.parser.parse_args(
            [
                'start', '-f', '-o', self.temp_dir,
                'example-fuzzers/crash_fuzzer'
            ])
        fuzzer = self.factory.create_fuzzer(cmd)
        fuzzer.corpus.reset()
        num_added = len(fuzzer.corpus.add_from_host(corpus_dir))
        self.assertEqual(num_added, len(corpus))
        cmd.command(cmd, self.factory)

        minlen = min(len(v) for k, v in corpus.items())
        maxlen = max(len(v) for k, v in corpus.items())
        self.assertOutContains(
            'INFO: libFuzzer starting', '2 files found in data/corpus',
            'seed corpus: files: 2 min: {}b max: {}b'.format(minlen, maxlen))

    def test_dictionary(self):
        # Here we pass a timeout because this example isn't designed to crash
        # immediately
        cmd = self.parser.parse_args(
            [
                'start', '-f', '-o', self.temp_dir,
                'example-fuzzers/dictionary_fuzzer', '-max_total_time=1'
            ])
        self.factory.create_fuzzer(cmd).corpus.reset()
        cmd.command(cmd, self.factory)
        self.assertOutContains(
            'INFO: libFuzzer starting', 'Dictionary: 12 entries',
            '0 files found in data/corpus',
            '5 files found in pkg/data/examples/fuzzers/cpp/example-corpus')


class IntegrationTestSingle(IntegrationTest):
    """Exercise several basic operations with the given fuzzer."""

    def test_single_fuzzer(self):
        # (Re-)parse the command line arguments, a la main.py.
        args = self.parser.parse_args()

        # Ensure exactly 1 fuzzer is selected.
        fuzzer = self.factory.create_fuzzer(args)
        self.assertNoErrors()
        args.name = str(fuzzer)

        list_args = self.parser.parse_args(['list', args.name])
        list_args.command(list_args, self.factory)
        self.assertOut(
            ['Found 1 matching fuzzer for "{}":'.format(str(fuzzer))], n=1)
        self.assertNoErrors()

        start_args = self.parser.parse_args(
            ['start', '-o', self.temp_dir, args.name])
        proc = command.start_fuzzer(start_args, self.factory)
        self.assertNoErrors()

        stop_args = self.parser.parse_args(['stop', args.name])
        command.stop_fuzzer(stop_args, self.factory)
        self.assertNoErrors()
        if proc:
            proc.wait()

        check_args = self.parser.parse_args(['check', args.name])
        command.check_fuzzer(check_args, self.factory)
        self.assertOut(['{}: STOPPED'.format(args.name)], n=1)
        self.assertNoErrors()

        unit = os.path.join(self.temp_dir, 'unit')
        with open(unit, 'w') as opened:
            opened.write('hello world')

        repro_args = self.parser.parse_args(['repro', args.name, unit])
        command.repro_units(repro_args, self.factory)
        self.assertNoErrors()

        analyze_args = ['analyze', '-max_total_time=10', args.name]
        if args.local:
            analyze_args.append('--local')
        analyze_args = self.parser.parse_args(analyze_args)
        command.analyze_fuzzer(analyze_args, self.factory)
        self.assertNoErrors()


if __name__ == '__main__':
    unittest.main()
