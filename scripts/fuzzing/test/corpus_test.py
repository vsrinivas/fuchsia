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

    def test_generate_buildfile(self):
        # Fuzzer without corpus directory specified in its metadata.
        fuzzer1 = self.create_fuzzer('fake-package1/fake-target1')
        self.assertError(
            lambda: fuzzer1.corpus.generate_buildfile(),
            'No corpus set for {}.'.format(str(fuzzer1)))

        # Missing directory
        fuzzer2 = self.create_fuzzer('fake-package1/fake-target2')
        corpus_dir = self.buildenv.path(fuzzer2.corpus.srcdir)
        self.assertError(
            lambda: fuzzer2.corpus.generate_buildfile(),
            'No such directory: {}'.format(corpus_dir))

        # Fuzzer with empty corpus
        build_gn = self.buildenv.path(corpus_dir, 'BUILD.gn')
        self.host.mkdir(corpus_dir)
        self.assertFalse(self.host.isfile(build_gn))
        self.assertEqual(fuzzer2.corpus.generate_buildfile(), [])
        self.assertTrue(self.host.isfile(build_gn))

        with self.host.open(build_gn) as f:
            contents = f.read()
        self.assertNotIn('foo,', contents)
        self.assertNotIn('bar,', contents)
        self.assertNotIn('baz,', contents)
        self.assertIn('sources = []', contents)
        self.assertIn(
            'outputs = [ "data/{}/{{{{source_file_part}}}}" ]'.format(
                fuzzer2.executable), contents)

        # Add elements to corpus and update
        self.host.touch(self.buildenv.path(corpus_dir, 'foo'))
        self.host.touch(self.buildenv.path(corpus_dir, 'bar'))
        self.assertEqual(fuzzer2.corpus.generate_buildfile(), ['bar', 'foo'])
        self.assertTrue(self.host.isfile(build_gn))
        with self.host.open(build_gn) as f:
            contents = f.read()
        self.assertIn('"foo",', contents)
        self.assertIn('"bar",', contents)
        self.assertNotIn('baz,', contents)
        self.assertIn(
            'outputs = [ "data/{}/{{{{source_file_part}}}}" ]'.format(
                fuzzer2.executable), contents)

        # Use an existing GN file in a different location.
        self.host.remove(build_gn)
        build_gn_dir = self.buildenv.path('src', 'fake')
        build_gn = os.path.join(build_gn_dir, 'new.gn')
        corpus_relpath2 = os.path.relpath(corpus_dir, build_gn_dir)
        with self.host.open(build_gn, 'w') as f:
            f.write('some existing data\n')
            f.write('more existing data\n')
        self.assertEqual(
            fuzzer2.corpus.generate_buildfile(build_gn=build_gn), [
                '{}/bar'.format(corpus_relpath2),
                '{}/foo'.format(corpus_relpath2),
            ])
        with self.host.open(build_gn) as f:
            contents = f.readlines()
        self.assertEqual(
            contents, [
                'some existing data\n',
                'more existing data\n',
                '\n',
                '# Generated using `fx fuzz update {} -o {}`.\n'.format(
                    str(fuzzer2), build_gn),
                'resource("{}_corpus") {{\n'.format(fuzzer2.executable),
                '  sources = [\n',
                '    "{}/bar",\n'.format(corpus_relpath2),
                '    "{}/foo",\n'.format(corpus_relpath2),
                '  ]\n',
                '  outputs = [ "data/{}/{{{{source_file_part}}}}" ]\n'.format(
                    fuzzer2.executable),
                '}\n',
            ])

        # Add another corpus to the same GN file. The existing one shouldn't be touched.
        fuzzer3 = self.create_fuzzer('fake-package1/fake-target3')
        corpus_dir = self.buildenv.path(fuzzer3.corpus.srcdir)
        corpus_relpath3 = os.path.relpath(corpus_dir, build_gn_dir)
        self.host.mkdir(corpus_dir)
        self.host.touch(self.buildenv.path(corpus_dir, 'baz'))
        self.assertEqual(
            fuzzer3.corpus.generate_buildfile(build_gn=build_gn),
            ['{}/baz'.format(corpus_relpath3)])
        with self.host.open(build_gn) as f:
            contents = f.readlines()
        self.assertEqual(
            contents, [
                'some existing data\n',
                'more existing data\n',
                '\n',
                '# Generated using `fx fuzz update {} -o {}`.\n'.format(
                    str(fuzzer2), build_gn),
                'resource("{}_corpus") {{\n'.format(fuzzer2.executable),
                '  sources = [\n',
                '    "{}/bar",\n'.format(corpus_relpath2),
                '    "{}/foo",\n'.format(corpus_relpath2),
                '  ]\n',
                '  outputs = [ "data/{}/{{{{source_file_part}}}}" ]\n'.format(
                    fuzzer2.executable),
                '}\n',
                '\n',
                '# Generated using `fx fuzz update {} -o {}`.\n'.format(
                    str(fuzzer3), build_gn),
                'resource("{}_corpus") {{\n'.format(fuzzer3.executable),
                '  sources = [ "{}/baz" ]\n'.format(corpus_relpath3),
                '  outputs = [ "data/{}/{{{{source_file_part}}}}" ]\n'.format(
                    fuzzer3.executable),
                '}\n',
            ])


if __name__ == '__main__':
    unittest.main()
