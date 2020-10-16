#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os


class Dictionary(object):
    """Represents the dictionary of keywords for a fuzzer.

       See https://llvm.org/docs/LibFuzzer.html#dictionaries for details on
       fuzzer dictionaries. Note that libFuzzer supports at most one"-dict=DICT"
       option when being run.

    Attributes:
        fuzzer:         The Fuzzer corresponding to this object.
        ns:             Alias for fuzzer.ns.
        nspath:         Path to dictionary in the namespace.
  """

    def __init__(self, fuzzer):
        self._fuzzer = fuzzer
        self._nspath = None

    @property
    def fuzzer(self):
        """The Fuzzer corresponding to this object."""
        return self._fuzzer

    @property
    def host(self):
        """Alias for fuzzer.host."""
        return self.fuzzer.host

    @property
    def ns(self):
        """Alias for fuzzer.ns."""
        return self.fuzzer.ns

    @property
    def nspath(self):
        """Path to dictionary in the namespace."""
        if not self._nspath:
            self.find_on_device()
        return self._nspath

    def find_on_device(self):
        resource = self.ns.resource(self.fuzzer.executable + '/dictionary')
        if self.ns.ls(resource):
            self._nspath = resource

    def replace(self, pathname):
        if not self.host.isfile(pathname):
            self.host.error('No such file: {}'.format(pathname))
        relpath = os.path.basename(pathname)
        self._nspath = self.ns.data(relpath)
        self.ns.store(self._nspath, pathname)
