#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

import test_env
from test_case import TestCase


class CorpusTest(TestCase):

    def setUp(self):
        super(CorpusTest, self).setUp()
        self.create_fuzzer('check', 'fake-package1/fake-target3')
        self.ns = self.fuzzer.ns
        self.corpus = self.fuzzer.corpus

    def test_add_from_host(self):
        relpath = 'relpath'
        corpus_element = os.path.join(relpath, 'element')
        self.cli.touch(corpus_element)
        self.corpus.add_from_host(relpath)
        self.assertScpTo(corpus_element, self.data_abspath('corpus'))

    def test_add_from_gcs(self):
        gcs_url = 'gs://bucket'
        # Note: this takes advantage of the fact that the FakeCLI always returns
        # the same name for temp_dir().
        with self.cli.temp_dir() as temp_dir:
            corpus_element = os.path.join(temp_dir.pathname, 'element')
            self.cli.touch(corpus_element)
            self.corpus.add_from_gcs(gcs_url)
            cmd = ['gsutil', '-m', 'cp', gcs_url + '/*', temp_dir.pathname]
            self.assertRan(*cmd)
            self.assertScpTo(corpus_element, self.data_abspath('corpus'))

    def test_measure(self):
        cmd = ['ls', '-l', self.data_abspath('corpus')]
        self.set_outputs(
            cmd, [
                '-rw-r--r-- 1 0 0 1796 Mar 19 17:25 foo',
                '-rw-r--r-- 1 0 0  124 Mar 18 22:02 bar',
            ],
            ssh=True)
        sizes = self.corpus.measure()
        self.assertEqual(sizes, (2, 1796 + 124))


if __name__ == '__main__':
    unittest.main()
