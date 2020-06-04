#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import subprocess

import test_env
from lib.cipd import Cipd
from lib.corpus import Corpus
from lib.fuzzer import Fuzzer

from device_fake import FakeDevice


class FakeCipd(Cipd):

    def __init__(self):
        fuzzer = Fuzzer(FakeDevice(), u'fake-package1', u'fake-target3')
        self.versions = []
        super(FakeCipd, self).__init__(Corpus(fuzzer))

    def _cipd(self, cmd, cwd=None, quiet=False):
        """Overrides Cipd._cipd for testing."""
        super(FakeCipd, self)._cipd(cmd, cwd, quiet)
        if cmd[0] == 'install' or cmd[0] == 'create':
            return 'ok'
        elif cmd[0] == 'instances':
            return r"""
Instance ID  | Timestamp | Uploader               | Refs
----------------------------------------------------------------
some-version | some-time | some-author@google.com | latest
"""
        elif cmd[0] == 'search' and cmd[-1] == 'integration:some-revision':
            return """
Instances:
  fuchsia/test_data/fuzzing/%s:some-version
""" % str(self.corpus.fuzzer)
        elif cmd[0] == 'describe' and cmd[-1] in self.versions:
            return cmd[-1]
        else:
            raise subprocess.CalledProcessError(-1, ' '.join(cmd), None)

    def add_version(self, version):
        self.versions.append(version)
