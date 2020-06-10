#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from StringIO import StringIO

import test_env
from lib.fuzzer import Fuzzer

from device_fake import FakeDevice


class FakeFuzzer(Fuzzer):
    """A fake for isolating fuzzers from the filesystem.

       Instead of actually reading and writing files, this fake provides methods
       to inject StringIO objects.

       Attributes:
         unsymbolized:      A file-like object that this object symbolizes.
         symbolized:        A file-like object containing symbolizer output.
    """

    def __init__(self, device, package, executable):
        super(FakeFuzzer, self).__init__(device, package, executable)
        device.host.mkdir(self.output)
        self._unsymbolized = StringIO()
        self._symbolized = None

    @property
    def unsymbolized(self):
        """A file-like object that this object symbolizes."""
        return self._unsymbolized

    @property
    def symbolized(self):
        """A file-like object containing symbolizer output."""
        return self._symbolized

    def symbolize_log(
            self,
            fd_in=None,
            filename_in=None,
            fd_out=None,
            filename_out=None,
            echo=False):
        """Symbolizes self.unsymbolized to self.symbolized.

        After calling, self.unsymbolized is cleared and reset.
        """
        self._symbolized = StringIO()
        self._unsymbolized.seek(0)
        result = self._symbolize_log_impl(
            fd_in=self._unsymbolized, fd_out=self._symbolized, echo=echo)
        self._symbolized.seek(0)
        self._unsymbolized = StringIO()
        return result
