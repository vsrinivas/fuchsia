#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import argparse
import tempfile
import shutil
import errno

from lib.host import Host
from lib.fuzzer import Fuzzer


class Corpus(object):
    """Represents a corpus of fuzzing inputs.

    A fuzzing corpus is the set of "interesting" inputs as determined by the
    individual fuzzer.  See https://llvm.org/docs/LibFuzzer.html#corpus for
    details on how libFuzzer uses corpora.

    This class acts as a context manager to ensure temporary root directories
    are cleaned up.

    Attributes:
        fuzzer: The fuzzer corresponding to this corpus instance.
        root: Local directory where CIPD packages can be assembled or unpacked.
          If not specified by the command line arguments, this will be a
          temporary directory.
  """

    @classmethod
    def from_args(cls, fuzzer, args):
        """Constructs a Cipd from command line arguments."""
        return cls(fuzzer, args.staging)

    def __init__(self, fuzzer, root=None):
        self.fuzzer = fuzzer
        if root:
            self.root = root
            self._is_tmp = False
            self.fuzzer.device.host.mkdir(root)
        else:
            self.root = tempfile.mkdtemp()
            self._is_tmp = True

    def __enter__(self):
        return self

    def __exit__(self, e_type, e_value, traceback):
        if self._is_tmp:
            self.fuzzer.device.host.rmdir(self.root, recursive=True)

    def push(self):
        """Copy the corpus to the fuzzer's device."""
        host_src = os.path.join(self.root, '*')
        data_dst = self.fuzzer.data_path('corpus')
        self.fuzzer.require_stopped()
        self.fuzzer.device.store(host_src, data_dst)

    def pull(self):
        """Copy the corpus from the fuzzer's device."""
        data_src = self.fuzzer.data_path('corpus/*')
        host_dst = self.root
        self.fuzzer.require_stopped()
        self.fuzzer.device.fetch(data_src, host_dst)
