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

    def test_instances(self):
        fake_cipd = FakeCipd()
        corpus = fake_cipd.corpus
        fuzzer = corpus.fuzzer
        host = fuzzer.device.host

        output = fake_cipd.instances()
        self.assertIn(
            fake_cipd._bin + ' instances fuchsia/test_data/fuzzing/' +
            str(fuzzer), host.history)
        self.assertIn('some-version', output)

    def test_install(self):
        fake_cipd = FakeCipd()
        corpus = fake_cipd.corpus
        fuzzer = corpus.fuzzer
        host = fuzzer.device.host

        self.assertFalse(fake_cipd.install('latest'))

        fake_cipd.add_version('latest')
        self.assertTrue(fake_cipd.install('latest'))
        self.assertIn(
            'CWD=' + corpus.root + ' ' + fake_cipd._bin +
            ' install fuchsia/test_data/fuzzing/' + str(fuzzer) + ' latest',
            host.history)

        fake_cipd.add_version('some-version')
        fake_cipd.install('integration:some-revision')
        self.assertIn(
            ' '.join(
                [
                    'CWD=' + corpus.root, fake_cipd._bin, 'install',
                    'fuchsia/test_data/fuzzing/' + str(fuzzer), 'some-version'
                ]), host.history)

    def test_create(self):
        fake_cipd = FakeCipd()
        corpus = fake_cipd.corpus
        host = corpus.fuzzer.device.host

        fake_cipd.create()
        self.assertIn(
            fake_cipd._bin + ' create --pkg-def ' +
            os.path.join(corpus.root, 'cipd.yaml') +
            ' --ref latest --tag integration:' + host.snapshot(), host.history)


if __name__ == '__main__':
    unittest.main()
