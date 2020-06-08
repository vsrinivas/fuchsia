#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import unittest
import tempfile

import test_env
from lib.args import ArgParser
from lib.corpus import Corpus
from lib.fuzzer import Fuzzer

from device_fake import FakeDevice


class TestCorpus(unittest.TestCase):

    # Unit tests

    def test_from_args(self):
        fuzzer = Fuzzer(FakeDevice(), u'fake-package1', u'fake-target3')
        parser = ArgParser('description')

        args = parser.parse_args(['1/3'])
        corpus = Corpus.from_args(fuzzer, args)
        self.assertTrue(os.path.exists(corpus.root))

        tmp_dir = tempfile.mkdtemp()
        try:
            args = parser.parse_args(['1/3', '--staging', tmp_dir])
            corpus = Corpus.from_args(fuzzer, args)
            self.assertEqual(tmp_dir, corpus.root)
        finally:
            shutil.rmtree(tmp_dir)

    def test_push(self):
        device = FakeDevice()
        fuzzer = Fuzzer(device, u'fake-package1', u'fake-target3')
        parser = ArgParser('description')

        args = parser.parse_args(['1/3'])
        corpus = Corpus.from_args(fuzzer, args)
        with tempfile.NamedTemporaryFile(dir=corpus.root) as f:
            device.host.pathnames.append(os.path.join(corpus.root, f.name))
            corpus.push()
            self.assertIn(
                ' '.join(
                    device._scp_cmd(
                        [f.name, '[::1]:' + fuzzer.data_path('corpus')])),
                device.host.history)

    def test_pull(self):
        device = FakeDevice()
        fuzzer = Fuzzer(device, u'fake-package1', u'fake-target3')
        parser = ArgParser('description')

        args = parser.parse_args(['1/3'])
        corpus = Corpus.from_args(fuzzer, args)
        corpus.pull()
        self.assertIn(
            ' '.join(
                device._scp_cmd(
                    ['[::1]:' + fuzzer.data_path('corpus/*'), corpus.root])),
            device.host.history)


if __name__ == '__main__':
    unittest.main()
