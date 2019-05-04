#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess

import test_env
from lib.device import Device

from host_mock import MockHost


class MockDevice(Device):

  def __init__(self):
    super(MockDevice, self).__init__(MockHost(), '::1')
    self.history = []
    self.toggle = False

  def _ssh(self, cmdline, stdout=subprocess.PIPE):
    """ Overrides Device._ssh to provide canned responses."""
    self.history.append(' '.join(
        self.get_ssh_cmd(['ssh', self._addr] + cmdline)))
    if cmdline[0] == 'cs' and self.toggle:
      response = """
  http.cmx[20963]: fuchsia-pkg://fuchsia.com/http#m
  mock-target1.cmx[7412221]: fuchsia-pkg://fuchsia.com/mock-p
  mock-target2.cmx[7412222]: fuchsia-pkg://fuchsia.com/mock-p
  an-extremely-verbose-target-name[7412223]: fuchsia-pkg://fuchsia.com/mock-p
"""
      self.toggle = False
    elif cmdline[0] == 'cs':
      response = """
  http.cmx[20963]: fuchsia-pkg://fuchsia.com/http#m
  mock-target1.cmx[7412221]: fuchsia-pkg://fuchsia.com/mock-p
  an-extremely-verbose-target-name[7412223]: fuchsia-pkg://fuchsia.com/mock-p
"""
      self.toggle = True
    elif cmdline[0] == 'ls' and cmdline[-1].endswith('corpus'):
      response = """
-rw-r--r--    1 0        0              1796 Mar 19 17:25 feac37187e77ff60222325cf2829e2273e04f2ea
-rw-r--r--    1 0        0               124 Mar 18 22:02 ff415bddb30e9904bccbbd21fb5d4aa9bae9e5a5
"""
    elif cmdline[0] == 'ls':
      response = """
drw-r--r--    2 0        0             13552 Mar 20 01:40 corpus
-rw-r--r--    1 0        0               918 Mar 20 01:40 fuzz-0.log
-rw-r--r--    1 0        0              1337 Mar 20 01:40 crash-deadbeef
-rw-r--r--    1 0        0              1729 Mar 20 01:40 leak-deadfa11
-rw-r--r--    1 0        0             31415 Mar 20 01:40 oom-feedface
"""
    else:
      response = ''
    return subprocess.Popen(
        ['printf', '\'' + response + '\''],
        stdout=stdout,
        stderr=subprocess.STDOUT)

  def _scp(self, src, dst):
    """ Overrides Device._scp."""
    self.history.append(' '.join(self.get_ssh_cmd(['scp', src, dst])))
