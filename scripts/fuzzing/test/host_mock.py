#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

import test_env
from lib.host import Host

from process_mock import MockProcess


class MockHost(Host):

    def __init__(self):
        super(MockHost, self).__init__()
        self._ids = os.path.join('mock', 'ids.txt')
        self._llvm_symbolizer = os.path.join('mock', 'llvm_symbolizer')
        self._symbolizer_exec = os.path.join('mock', 'symbolize')
        self._platform = 'mock'
        self._zxtools = os.path.join('mock', 'out', 'default.zircon', 'tools')
        self.ssh_config = os.path.join(
            'mock', 'out', 'default', 'ssh-keys', 'ssh_config')
        self.fuzzers = [
            (u'mock-package1', u'mock-target1'),
            (u'mock-package1', u'mock-target2'),
            (u'mock-package1', u'mock-target3'),
            (u'mock-package2', u'mock-target1'),
            (u'mock-package2', u'mock-target11'),
            (u'mock-package2', u'an-extremely-verbose-target-name')
        ]
        self.history = []

    def create_process(self, args, **kwargs):
        return MockProcess(self, args, **kwargs)
