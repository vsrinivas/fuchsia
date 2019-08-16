#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import unittest
import tempfile

import test_env
from lib.args import Args
from lib.cipd import Cipd
from lib.fuzzer import Fuzzer

from cipd_mock import MockCipd
from device_mock import MockDevice


class TestCipd(unittest.TestCase):

    def test_instances(self):
        mock_cipd = MockCipd()
        corpus = mock_cipd.corpus

        output = mock_cipd.instances()
        self.assertIn(
            mock_cipd._bin + ' instances fuchsia/test_data/fuzzing/' + str(
                corpus.fuzzer), mock_cipd.history)
        self.assertIn('some-version', output)

    def test_install(self):
        mock_cipd = MockCipd()
        corpus = mock_cipd.corpus

        self.assertFalse(mock_cipd.install('latest'))

        mock_cipd.add_version('latest')
        self.assertTrue(mock_cipd.install('latest'))
        self.assertIn(
            'CWD=' + corpus.root + ' ' +
            mock_cipd._bin + ' install fuchsia/test_data/fuzzing/' + str(
                corpus.fuzzer) + ' latest', mock_cipd.history)

        mock_cipd.add_version('some-version')
        mock_cipd.install('integration:some-revision')
        self.assertIn(
            ' '.join(
                [
                    'CWD=' + corpus.root, mock_cipd._bin, 'install',
                    'fuchsia/test_data/fuzzing/' + str(corpus.fuzzer),
                    'some-version'
                ]), mock_cipd.history)

    def test_create(self):
        mock_cipd = MockCipd()
        corpus = mock_cipd.corpus
        host = corpus.fuzzer.device.host

        mock_cipd.create()
        self.assertIn(
            mock_cipd._bin + ' create --pkg-def ' + os.path.join(
                corpus.root, 'cipd.yaml') + ' --ref latest --tag integration:' +
            host.snapshot(), mock_cipd.history)


if __name__ == '__main__':
    unittest.main()
