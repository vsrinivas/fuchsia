#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os


class Namespace(object):
    """Represents the package filesystem namespace for a fuzzer.

    The fuzzer executable's view of the filesystem is different from the
    device's. When the fuzzer is run as a component, certain paths get mapped
    to certain locations in the fuzzer's namespace. This object represents and
    encapsulates that mapping for code that needs to move data between the
    device and the host.

    Parameter names are chosen according to the following convetions:
        abspath         An global, absolute path outside any namespace.
        data_path       Path within a namespace mapping to mutable storage.
        resouce_path    Path within a namespace mapping to package resources.
        relpath         A path fragment relative to one of the other 3 contexts.

    e.g. Given relpath = "foo":
        data_path(relpath)          -> "data/foo"
        resouce_path(relpath)       -> "pkg/data/foo"
        abspath(data_path(relpath)) -> "/some/path/accesssible/by/scp/foo"

    Attributes:
        fuzzer:         The Fuzzer corresponding to this object.
        device:         Alias for fuzzer.device.
  """

    def __init__(self, fuzzer):
        self._fuzzer = fuzzer

    @property
    def fuzzer(self):
        """The associated Fuzzer object."""
        return self._fuzzer

    @property
    def device(self):
        """Alias for fuzzer.device."""
        return self.fuzzer.device

    @property
    def host(self):
        """Alias for fuzzer.host."""
        return self.fuzzer.host

    # Unit test utilities

    def base_abspath(self, relpath=''):
        """Absolute package path when package is included in the base image."""
        base = '/pkgfs/packages/{}/0'.format(self.fuzzer.package)
        return '{}/{}'.format(base, relpath) if relpath else base

    def data_abspath(self, relpath):
        """Absolute path to mutable data."""
        base = '/data/r/sys/fuchsia.com:{}:0#meta:{}.cmx'.format(
            self.fuzzer.package, self.fuzzer.executable)
        if relpath.startswith(base):
            return relpath
        elif relpath.startswith('/'):
            self.host.error('Not a data path: {}'.format(relpath))
        elif relpath.startswith(self.data()):
            relpath = relpath[len(self.data()):]
        return '{}/{}'.format(base, relpath)

    def resource_abspath(self, relpath):
        """Absolute path to a package resource."""
        base = self.fuzzer.package_path + '/data'
        if relpath.startswith(base):
            return relpath
        elif relpath.startswith('/'):
            self.host.error('Not a resource path: {}'.format(relpath))
        elif relpath.startswith(self.resource()):
            relpath = relpath[len(self.resource()):]
        return '{}/{}'.format(base, relpath)

    def abspath(self, nspath):
        if nspath.startswith(self.data()):
            return self.data_abspath(nspath)
        elif nspath.startswith(self.resource()):
            return self.resource_abspath(nspath)
        else:
            self.host.error('Not a data or resource path: {}'.format(nspath))

    def data(self, relpath=''):
        """Returns a namespace path to package data."""
        base = 'data'
        if relpath.startswith('base/'):
            return relpath
        return '{}/{}'.format(base, relpath)

    def resource(self, relpath=''):
        """Returns a namespace path to package resources."""
        base = 'pkg/data'
        if relpath.startswith(base):
            return relpath
        return '{}/{}'.format(base, relpath.lstrip('/'))

    def ls(self, nspath):
        """Lists a directory in the namespace."""
        return self.device.ls(self.abspath(nspath))

    def mkdir(self, data_path):
        """Makes a directory in the namespace."""
        self.device.mkdir(self.data_abspath(data_path))

    def remove(self, data_path, recursive=False):
        """Removes files or directories from the namespace."""
        return self.device.remove(self.data_abspath(data_path), recursive)

    def fetch(self, pathname, *args):
        """Copies from the namespace to the path given by the first argument."""
        args = [self.abspath(arg) for arg in args]
        self.device.fetch(pathname, *args)

    def store(self, data_path, *args):
        """Copies to the namespace path given by the first argument."""
        abspath = self.data_abspath(data_path)
        pathnames = self.device.store(abspath, *args)
        relpaths = [os.path.basename(pathname) for pathname in pathnames]
        return [self.data(relpath) for relpath in relpaths]
