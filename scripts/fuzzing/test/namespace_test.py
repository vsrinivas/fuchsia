#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

import test_env
from test_case import TestCaseWithFuzzer


class NamespaceTest(TestCaseWithFuzzer):

    def test_data(self):
        self.assertEqual(self.ns.data(), 'data/')
        self.assertEqual(self.ns.data('corpus'), 'data/corpus')

    def test_resource(self):
        self.assertEqual(self.ns.resource(), 'pkg/data/')
        self.assertEqual(self.ns.resource('something'), 'pkg/data/something')

    def test_base_abspath(self):
        self.assertEqual(
            self.ns.base_abspath('bin/fake-target1'),
            '/pkgfs/packages/fake-package1/0/bin/fake-target1')

    def test_data_abspath(self):
        relpath = 'foo'
        data = self.ns.data(relpath)
        abspath = self.ns.data_abspath(relpath)
        self.assertEqual(
            abspath, '/data/r/sys/fuchsia.com:{}:0#meta:{}.cmx/{}'.format(
                self.fuzzer.package, self.fuzzer.executable, relpath))
        self.assertEqual(abspath, self.ns.data_abspath(data))
        self.assertEqual(abspath, self.ns.data_abspath(abspath))

    def test_resource_abspath(self):
        relpath = 'foo'
        resource = self.ns.resource(relpath)
        abspath = self.ns.resource_abspath(relpath)
        self.assertEqual(
            abspath, '{}/data/{}'.format(self.fuzzer.package_path, relpath))
        self.assertEqual(abspath, self.ns.resource_abspath(resource))
        self.assertEqual(abspath, self.ns.resource_abspath(abspath))

    def test_abspath(self):
        relpath = 'foo'
        self.assertEqual(
            self.ns.abspath(self.ns.data(relpath)),
            self.ns.data_abspath(relpath))
        self.assertEqual(
            self.ns.abspath(self.ns.resource(relpath)),
            self.ns.resource_abspath(relpath))
        self.assertError(
            lambda: self.ns.abspath(relpath),
            'Not a data or resource path: {}'.format(relpath))

    def test_ls(self):
        relpath = 'corpus'

        # Must be data path
        self.assertError(
            lambda: self.ns.ls(relpath),
            'Not a data or resource path: {}'.format(relpath))

        # Valid
        self.touch_on_device(
            self.ns.data_abspath(relpath + '/deadbeef'), size=314)
        self.touch_on_device(
            self.ns.data_abspath(relpath + '/feedface'), size=159)
        files = self.ns.ls(self.ns.resource(relpath))

    def test_mkdir(self):
        relpath = 'some-dir'

        # Must be data path
        self.assertError(
            lambda: self.ns.mkdir(self.ns.resource_abspath(relpath)),
            'Not a data path: {}'.format(self.ns.resource_abspath(relpath)))

        # Valid
        self.ns.mkdir(self.ns.data(relpath))
        cmd = ['mkdir', '-p', self.ns.data_abspath(relpath)]
        self.assertSsh(*cmd)

    def test_remove(self):
        relpath = 'some-path'

        # Must be data path
        self.assertError(
            lambda: self.ns.remove(self.ns.resource_abspath(relpath)),
            'Not a data path: {}'.format(self.ns.resource_abspath(relpath)))

        # Valid file
        self.ns.remove(self.ns.data(relpath))
        cmd = ['rm', '-f', self.ns.data_abspath(relpath)]
        self.assertSsh(*cmd)

        # Valid directory
        self.ns.remove(self.ns.data(relpath), recursive=True)
        cmd = ['rm', '-rf', self.ns.data_abspath(relpath)]
        self.assertSsh(*cmd)

    def test_fetch(self):
        local_path = 'test_fetch'

        relpath1 = 'fuzz-0.log'
        data = self.ns.data(relpath1)

        relpath2 = 'corpus/*'
        resource = self.ns.resource(relpath2)

        # Local path must exist
        self.assertError(
            lambda: self.ns.fetch(local_path, data, resource),
            'No such directory: {}'.format(local_path))

        # Must be data or resource path(s)
        self.host.mkdir(local_path)
        self.assertError(
            lambda: self.ns.fetch(local_path, relpath1, resource),
            'Not a data or resource path: {}'.format(relpath1))

        self.assertError(
            lambda: self.ns.fetch(local_path, data, relpath2),
            'Not a data or resource path: {}'.format(relpath2))

        # Valid
        self.ns.fetch(local_path, data, resource)
        self.assertScpFrom(
            self.ns.data_abspath(relpath1), self.ns.resource_abspath(relpath2),
            local_path)

    def test_store(self):
        local_path = 'test_store'
        relpath = 'remote_path'

        # Globs must resolve
        data_path = self.ns.data(relpath)
        self.assertError(
            lambda: self.ns.store(data_path, os.path.join(local_path, '*')),
            'No matching files: "test_store/*".')

        # Local path must exist
        foo = os.path.join(local_path, 'foo')
        self.assertError(
            lambda: self.ns.store(data_path, foo),
            'No matching files: "test_store/foo".')

        # Must be data path(s)
        self.host.touch(foo)
        self.assertError(
            lambda: self.ns.store(self.ns.resource_abspath(relpath), foo),
            'Not a data path: {}'.format(self.ns.resource_abspath(relpath)))

        # Valid
        self.ns.store(data_path, foo)
        self.assertScpTo(foo, self.ns.data_abspath(relpath))

        # Valid globs
        bar = os.path.join(local_path, 'bar')
        baz = os.path.join(local_path, 'baz')
        self.host.touch(bar)
        self.host.touch(baz)
        datas = self.ns.store(data_path, os.path.join(local_path, '*'))
        self.assertScpTo(bar, baz, foo, self.ns.data_abspath(relpath))
        relpaths = [os.path.basename(pathname) for pathname in [bar, baz, foo]]
        self.assertEqual(datas, [self.ns.data(relpath) for relpath in relpaths])


if __name__ == '__main__':
    unittest.main()
