#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

import test_env
from test_case import FuzzerTestCase


class CorpusTest(FuzzerTestCase):

    @property
    def corpus(self):
        return self.fuzzer.corpus

    def test_add_from_host(self):
        # Invalid directory
        relpath = 'relpath'
        self.assertError(
            lambda: self.corpus.add_from_host(relpath),
            'No such directory: {}'.format(relpath))

        # Fuzzer is running
        self.cli.mkdir(relpath)
        corpus_element = os.path.join(relpath, 'element')
        self.cli.touch(corpus_element)
        self.set_running(
            self.fuzzer.package, self.fuzzer.executable, duration=10)
        self.assertError(
            lambda: self.corpus.add_from_host(relpath),
            'fake-package1/fake-target1 is running and must be stopped first.')
        self.cli.sleep(10)

        # Valid
        added = self.corpus.add_from_host(relpath)
        self.assertEqual(len(added), 1)
        self.assertScpTo(corpus_element, self.data_abspath('corpus'))

    def test_add_from_gcs(self):
        # Note: this takes advantage of the fact that the FakeCLI always returns
        # the same name for temp_dir().
        with self.cli.temp_dir() as temp_dir:
            gcs_url = 'gs://bucket'
            cmd = ['gsutil', '-m', 'cp', gcs_url + '/*', temp_dir.pathname]
            process = self.get_process(cmd)
            process.succeeds = False
            self.assertError(
                lambda: self.corpus.add_from_gcs(gcs_url),
                'Failed to download corpus from GCS.',
                'You can skip downloading from GCS with the "--local" flag.')

            process.succeeds = True
            corpus_element = os.path.join(temp_dir.pathname, 'element')
            self.cli.touch(corpus_element)
            added = self.corpus.add_from_gcs(gcs_url)
            self.assertEqual(len(added), 1)
            self.assertRan(*cmd)
            self.assertScpTo(corpus_element, self.data_abspath('corpus'))

    def test_measure(self):
        cmd = ['ls', '-l', self.ns.abspath(self.ns.data('corpus'))]
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
