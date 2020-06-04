#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

import test_env
from lib.host import Host

from process_fake import FakeProcess


class FakeHost(Host):

    def __init__(self):
        super(FakeHost, self).__init__()
        self._ids = [os.path.join('fake', '.build-id')]
        self._llvm_symbolizer = os.path.join('fake', 'llvm_symbolizer')
        self._symbolizer_exec = os.path.join('fake', 'symbolize')
        self._platform = 'fake'
        self._zxtools = os.path.join('fake', 'out', 'default.zircon', 'tools')
        self.ssh_config = os.path.join(
            'fake', 'out', 'default', 'ssh-keys', 'ssh_config')
        self.fuzzers = [
            (u'fake-package1', u'fake-target1'),
            (u'fake-package1', u'fake-target2'),
            (u'fake-package1', u'fake-target3'),
            (u'fake-package2', u'fake-target1'),
            (u'fake-package2', u'fake-target11'),
            (u'fake-package2', u'an-extremely-verbose-target-name')
        ]
        self.history = []

    def create_process(self, args, **kwargs):
        p = FakeProcess(self, args, **kwargs)
        if ' '.join(args) == 'git rev-parse HEAD':
            p.response = 'da39a3ee5e6b4b0d3255bfef95601890afd80709'
        elif args[0] == self._symbolizer_exec:
            p.response = """[000001.234567][123][456][klog] INFO: Symbolized line 1
[000001.234568][123][456][klog] INFO: Symbolized line 2
[000001.234569][123][456][klog] INFO: Symbolized line 3
"""
        elif 'device-finder' in args[0]:
            p.response = '::1'
        return p
