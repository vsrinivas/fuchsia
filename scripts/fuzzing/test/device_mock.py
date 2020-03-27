#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess

import test_env
from lib.device import Device
from lib.process import Process

from host_mock import MockHost


class MockDevice(Device):

    def __init__(self, port=22):
        super(MockDevice, self).__init__(MockHost(), '::1', port)
        self.toggle = False
        self.delay = 0

    def ssh(self, cmdline):
        """ Overrides Device.ssh to provide canned responses."""
        p = super(MockDevice, self).ssh(cmdline)
        if cmdline[0] == 'cs' and self.toggle:
            p.response = r"""
  http.cmx[20963]: fuchsia-pkg://fuchsia.com/http#m
  mock-target1.cmx[7412221]: fuchsia-pkg://fuchsia.com/mock-p
  mock-target2.cmx[7412222]: fuchsia-pkg://fuchsia.com/mock-p
  an-extremely-verbose-target-name[7412223]: fuchsia-pkg://fuchsia.com/mock-p
"""
            self.toggle = False
        elif cmdline[0] == 'cs':
            p.response = r"""
  http.cmx[20963]: fuchsia-pkg://fuchsia.com/http#m
  mock-target1.cmx[7412221]: fuchsia-pkg://fuchsia.com/mock-p
  an-extremely-verbose-target-name[7412223]: fuchsia-pkg://fuchsia.com/mock-p
"""
            self.toggle = True
        elif cmdline[0] == 'ls' and cmdline[-1].endswith('corpus'):
            p.response = r"""
-rw-r--r--    1 0        0              1796 Mar 19 17:25 feac37187e77ff60222325cf2829e2273e04f2ea
-rw-r--r--    1 0        0               124 Mar 18 22:02 ff415bddb30e9904bccbbd21fb5d4aa9bae9e5a5
"""
        elif cmdline[0] == 'ls':
            p.response = r"""
drw-r--r--    2 0        0             13552 Mar 20 01:40 corpus
-rw-r--r--    1 0        0               918 Mar 20 01:40 fuzz-0.log
-rw-r--r--    1 0        0              1337 Mar 20 01:40 crash-deadbeef
-rw-r--r--    1 0        0              1729 Mar 20 01:40 leak-deadfa11
-rw-r--r--    1 0        0             31415 Mar 20 01:40 oom-feedface
"""
        elif cmdline[0] == 'log_listener':
            p.response = r"""
[0:0] {{{reset}}}
[0:0] {{{a line to symbolize}}}
[0:0] {{{another line to symbolize}}}
[0:0] {{{yet another line to symbolize}}}
"""
        return p

    def _scp(self, srcs, dst):
        """ Overrides Device._scp to simulate delayed file creation."""
        if len(srcs) == 1 and srcs[0].endswith('delayed') and self.delay != 0:
            self.delay -= 1
            raise subprocess.CalledProcessError(1, 'scp', 'mock failure')
        else:
            super(MockDevice, self)._scp(srcs, dst)
