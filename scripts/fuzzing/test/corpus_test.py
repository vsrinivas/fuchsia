#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

import test_env
from test_case import TestCaseWithFuzzer


class CorpusTest(TestCaseWithFuzzer):

    def test_find_on_device(self):
        data = self.ns.data('corpus')
        resource = self.ns.resource('corpus')

        self.corpus.find_on_device()
        self.assertEqual(self.corpus.nspaths, [data])

        self.touch_on_device(self.ns.resource_abspath('corpus/deadbeef'))
        self.corpus.find_on_device()
        self.assertEqual(self.corpus.nspaths, [data, resource])

    def test_add_from_host(self):
        # Invalid directory
        local_path = 'corpus_dir'
        self.assertError(
            lambda: self.corpus.add_from_host(local_path),
            'No such directory: {}'.format(local_path))
        self.host.mkdir(local_path)

        # Fuzzer is running
        corpus_element = os.path.join(local_path, 'element')
        self.host.touch(corpus_element)
        self.set_running(self.fuzzer.executable_url, duration=10)
        self.assertError(
            lambda: self.corpus.add_from_host(local_path),
            'fake-package1/fake-target1 is running and must be stopped first.')
        self.host.sleep(10)

        # Valid
        added = self.corpus.add_from_host(local_path)
        self.assertEqual(len(added), 1)
        self.assertScpTo(
            corpus_element, self.ns.data_abspath(self.corpus.nspaths[0]))

    def test_add_from_gcs(self):
        # Note: this takes advantage of the fact that the FakeCLI always returns
        # the same name for temp_dir().
        with self.host.temp_dir() as temp_dir:
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
            self.host.touch(corpus_element)
            added = self.corpus.add_from_gcs(gcs_url)
            self.assertEqual(len(added), 1)
            self.assertRan(*cmd)
            self.assertScpTo(
                corpus_element, self.ns.data_abspath(self.corpus.nspaths[0]))

    def test_measure(self):
        self.touch_on_device(self.ns.data_abspath('corpus/deadbeef'), size=1000)
        self.touch_on_device(self.ns.data_abspath('corpus/feedface'), size=729)
        sizes = self.corpus.measure()
        self.assertEqual(sizes, (2, 1 + 1728))


if __name__ == '__main__':
    unittest.main()
