#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import unittest
import tempfile

import test_env
from lib.cipd import Cipd
from lib.fuzzer import Fuzzer

from cipd_fake import FakeCipd
from device_fake import FakeDevice


class TestCipd(unittest.TestCase):

    # Unit tests

    def test_instances(self):
        cipd = FakeCipd()
        corpus = cipd.corpus
        fuzzer = corpus.fuzzer
        host = fuzzer.device.host

        output = cipd.instances()
        self.assertIn(
            cipd._bin + ' instances fuchsia/test_data/fuzzing/' + str(fuzzer),
            host.history)
        self.assertIn('some-version', output)

    def test_install(self):
        cipd = FakeCipd()
        corpus = cipd.corpus
        fuzzer = corpus.fuzzer
        host = fuzzer.device.host

        self.assertFalse(cipd.install('latest'))

        cipd.add_version('latest')
        self.assertTrue(cipd.install('latest'))
        self.assertIn(
            'CWD=' + corpus.root + ' ' + cipd._bin +
            ' install fuchsia/test_data/fuzzing/' + str(fuzzer) + ' latest',
            host.history)

        cipd.add_version('some-version')
        cipd.install('integration:some-revision')
        self.assertIn(
            ' '.join(
                [
                    'CWD=' + corpus.root, cipd._bin, 'install',
                    'fuchsia/test_data/fuzzing/' + str(fuzzer), 'some-version'
                ]), host.history)

    def test_create(self):
        cipd = FakeCipd()
        corpus = cipd.corpus
        host = corpus.fuzzer.device.host

        cipd.create()
        self.assertIn(
            cipd._bin + ' create --pkg-def ' +
            os.path.join(corpus.root, 'cipd.yaml') +
            ' --ref latest --tag integration:' + host.snapshot(), host.history)


if __name__ == '__main__':
    unittest.main()
