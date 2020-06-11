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
    def cli(self):
        """Alias for fuzzer.cli."""
        return self._fuzzer.cli

    def abspath(self, data_path):
        if data_path.startswith(self.data()):
            relpath = data_path[len(self.data()):]
            return '/data/r/sys/fuchsia.com:{}:0#meta:{}.cmx/{}'.format(
                self.fuzzer.package, self.fuzzer.executable, relpath)
        else:
            self.cli.error('Not a data path: {}'.format(data_path))

    def data(self, relpath=''):
        """Returns a namespace path to package data."""
        return 'data/{}'.format(relpath)

    def resource(self, relpath=''):
        """Returns a namespace path to package resources."""
        return 'pkg/data/{}/{}'.format(self.fuzzer.executable, relpath)

    def ls(self, data_path=''):
        """Lists a directory in the namespace."""
        return self.device.ls(self.abspath(data_path))

    def mkdir(self, data_path):
        """Makes a directory in the namespace."""
        self.device.mkdir(self.abspath(data_path))

    def remove(self, data_path, recursive=False):
        """Removes files or directories from the namespace."""
        return self.device.remove(self.abspath(data_path), recursive)

    def fetch(self, pathname, *args):
        """Copies from the namespace to the path given by the first argument."""
        args = [self.abspath(arg) for arg in args]
        self.device.fetch(pathname, *args)

    def store(self, data_path, *args):
        """Copies to the namespace path given by the first argument."""
        pathnames = self.device.store(self.abspath(data_path), *args)
        relpaths = [os.path.basename(pathname) for pathname in pathnames]
        return [self.data(relpath) for relpath in relpaths]
