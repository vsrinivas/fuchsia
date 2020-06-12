#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

import test_env
from test_case import TestCaseWithFuzzer


class NamespaceTest(TestCaseWithFuzzer):

    def test_abspath(self):
        self.assertEqual(
            self.ns.abspath(self.ns.data('foo')),
            '/data/r/sys/fuchsia.com:fake-package1:0#meta:fake-target1.cmx/foo')

        self.assertError(lambda: self.ns.abspath('bar'), 'Not a data path: bar')

    def test_data(self):
        self.assertEqual(self.ns.data(), 'data/')
        self.assertEqual(self.ns.data('corpus'), 'data/corpus')

    def test_resource(self):
        self.assertEqual(self.ns.resource(), 'pkg/data/fake-target1/')
        self.assertEqual(
            self.ns.resource('dictionary'), 'pkg/data/fake-target1/dictionary')

    def test_ls(self):
        # Must be data path
        self.assertError(lambda: self.ns.ls('foo'), 'Not a data path: foo')

        # Valid
        cmd = ['ls', '-l', self.ns.abspath(self.ns.data('bar'))]
        self.set_outputs(
            cmd, [
                '-rw-r--r-- 1 0 0 1796 Mar 19 17:25 feac37187e77ff60222325cf2829e2273e04f2ea',
                '-rw-r--r-- 1 0 0  124 Mar 18 22:02 ff415bddb30e9904bccbbd21fb5d4aa9bae9e5a5',
            ],
            ssh=True)
        files = self.ns.ls(self.ns.data('bar'))
        self.assertSsh(*cmd)
        self.assertEqual(
            files['feac37187e77ff60222325cf2829e2273e04f2ea'], 1796)

    def test_mkdir(self):
        # Must be data path
        self.assertError(lambda: self.ns.mkdir('foo'), 'Not a data path: foo')

        # Valid
        cmd = ['mkdir', '-p', self.ns.abspath(self.ns.data('bar'))]
        self.ns.mkdir(self.ns.data('bar'))
        self.assertSsh(*cmd)

    def test_remove(self):
        # Must be data path
        self.assertError(lambda: self.ns.remove('foo'), 'Not a data path: foo')

        #  Valid file
        cmd = ['rm', '-f', self.ns.abspath(self.ns.data('foo'))]
        self.ns.remove(self.ns.data('foo'))
        self.assertSsh(*cmd)

        #  Valid directory
        cmd = ['rm', '-rf', self.ns.abspath(self.ns.data('bar'))]
        self.ns.remove(self.ns.data('bar'), recursive=True)
        self.assertSsh(*cmd)

    def test_fetch(self):
        local_path = 'test_fetch'

        relpath1 = 'corpus/*'
        data_path1 = self.ns.data(relpath1)
        abspath1 = self.data_abspath(relpath1)

        relpath2 = 'fuzz-0.log'
        data_path2 = self.ns.data(relpath2)
        abspath2 = self.data_abspath(relpath2)

        # Local path must exist
        self.assertError(
            lambda: self.ns.fetch(local_path, data_path1, data_path2),
            'No such directory: {}'.format(local_path))

        # Must be data path(s)
        self.cli.mkdir(local_path)
        self.assertError(
            lambda: self.ns.fetch(local_path, relpath1, data_path2),
            'Not a data path: {}'.format(relpath1))

        self.assertError(
            lambda: self.ns.fetch(local_path, data_path1, relpath2),
            'Not a data path: {}'.format(relpath2))

        # Valid
        self.ns.fetch(local_path, data_path1, data_path2)
        self.assertScpFrom(abspath1, abspath2, local_path)

    def test_store(self):
        local_path = 'test_store'
        relpath = 'remote_path'
        data_path = self.ns.data(relpath)
        abspath = self.ns.abspath(data_path)

        # Globs must resolve
        self.assertError(
            lambda: self.ns.store(data_path, os.path.join(local_path, '*')),
            'No matching files: "test_store/*".')

        # Local path must exist
        foo = os.path.join(local_path, 'foo')
        self.assertError(
            lambda: self.ns.store(data_path, foo),
            'No matching files: "test_store/foo".')

        # Must be data path(s)
        self.cli.touch(foo)
        self.assertError(
            lambda: self.ns.store(relpath, foo),
            'Not a data path: {}'.format(relpath))

        # Valid
        self.ns.store(data_path, foo)
        self.assertScpTo(foo, abspath)

        # Valid globs
        bar = os.path.join(local_path, 'bar')
        baz = os.path.join(local_path, 'baz')
        self.cli.touch(bar)
        self.cli.touch(baz)
        datas = self.ns.store(data_path, os.path.join(local_path, '*'))
        self.assertScpTo(bar, baz, foo, abspath)
        relpaths = [os.path.basename(pathname) for pathname in [bar, baz, foo]]
        self.assertEqual(datas, [self.ns.data(relpath) for relpath in relpaths])


if __name__ == '__main__':
    unittest.main()
