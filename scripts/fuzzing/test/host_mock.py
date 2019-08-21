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
        self._ids = [os.path.join('mock', '.build-id')]
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
        p = MockProcess(self, args, **kwargs)
        if ' '.join(args) == 'git rev-parse HEAD':
            p.response = 'da39a3ee5e6b4b0d3255bfef95601890afd80709'
        elif args[0] == self._symbolizer_exec:
            p.response = """[000001.234567][123][456][klog] INFO: Symbolized line 1
[000001.234568][123][456][klog] INFO: Symbolized line 2
[000001.234569][123][456][klog] INFO: Symbolized line 3
"""
        return p
