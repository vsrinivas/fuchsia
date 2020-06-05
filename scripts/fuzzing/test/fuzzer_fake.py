#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from StringIO import StringIO

import test_env
from lib.fuzzer import Fuzzer


class FakeFuzzer(Fuzzer):
    """A fake for isolating fuzzers from the filesystem.

       Instead of actually reading and writing files, this fake provides methods
       to inject StringIO objects.
    """

    def __init__(
            self,
            device,
            package,
            executable,
            output=None,
            foreground=False,
            debug=False):
        super(FakeFuzzer, self).__init__(
            device, package, executable, output, foreground, debug)
        self.unsymbolized = StringIO()
        self.symbolized = None

    def symbolize_log(
            self,
            fd_in=None,
            filename_in=None,
            fd_out=None,
            filename_out=None,
            echo=False):
        self.symbolized = StringIO()
        self.unsymbolized.seek(0)
        self._symbolize_log_impl(
            fd_in=self.unsymbolized, fd_out=self.symbolized, echo=echo)
        self.symbolized.seek(0)
        self.unsymbolized = StringIO()
