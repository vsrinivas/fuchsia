#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import unittest
import tempfile

import test_env
from lib.args import Args
from lib.corpus import Corpus
from lib.fuzzer import Fuzzer

from device_mock import MockDevice


class TestCorpus(unittest.TestCase):

    def test_from_args(self):
        fuzzer = Fuzzer(MockDevice(), u'mock-package1', u'mock-target3')
        parser = Args.make_parser('description')

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
        mock = MockDevice()
        fuzzer = Fuzzer(mock, u'mock-package1', u'mock-target3')
        parser = Args.make_parser('description')

        args = parser.parse_args(['1/3'])
        corpus = Corpus.from_args(fuzzer, args)
        with tempfile.NamedTemporaryFile(dir=corpus.root) as f:
            corpus.push()
            self.assertIn(
                ' '.join(
                    mock.get_ssh_cmd(
                        ['scp', f.name,
                         '[::1]:' + fuzzer.data_path('corpus')])), mock.history)

    def test_pull(self):
        mock = MockDevice()
        fuzzer = Fuzzer(mock, u'mock-package1', u'mock-target3')
        parser = Args.make_parser('description')

        args = parser.parse_args(['1/3'])
        corpus = Corpus.from_args(fuzzer, args)
        corpus.pull()
        self.assertIn(
            ' '.join(
                mock.get_ssh_cmd(
                    [
                        'scp', '[::1]:' + fuzzer.data_path('corpus/*'),
                        corpus.root
                    ])), mock.history)


if __name__ == '__main__':
    unittest.main()
