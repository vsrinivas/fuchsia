#!/usr/bin/env python3.8
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
from lib import command
from lib.host import Host
from lib.factory import Factory
from test_case import TestCaseWithIO


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
            self.assertEqual(f.read(3), b'HI!')

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
            f.write(struct.pack('<Q', 2) + b'AAAA')

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

    def test_minimize(self):
        corpus_element = os.path.join(self.temp_dir, 'overlong')
        with self.host.open(corpus_element, 'w') as f:
            # The crash is minimally triggered by 'HI!', but we have an extra
            # character here
            f.write('HI!!')

        cmd = self.parser.parse_args(
            [
                'repro', 'example-fuzzers/crash_fuzzer', corpus_element,
                '-exact_artifact_path=data/minimized', '-minimize_crash=1',
                '-max_total_time=5'
            ])
        fuzzer = self.factory.create_fuzzer(cmd)
        fuzzer.corpus.reset()
        cmd.command(cmd, self.factory)

        self.assertOutContains(
            'INFO: libFuzzer starting', 'Test unit written to data/minimized',
            'failed to minimize beyond data/minimized (3 bytes)')
