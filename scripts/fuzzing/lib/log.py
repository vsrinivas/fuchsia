#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import re
import subprocess

from host import Host


class Log(object):
  """Provides a context-managed interface to the fuzzing logs."""

  def __init__(self, fuzzer):
    self.fuzzer = fuzzer
    self.device = fuzzer.device
    self.host = fuzzer.host

  def __enter__(self):
    """Resets the fuzzing logs.

      This will clear stale logs from the device for the given fuzzer and
      restart loglistener.
    """
    self.device.ssh(['rm', self.fuzzer.data_path('fuzz-*.log')])
    subprocess.call(['killall', 'loglistener'], stderr=Host.DEVNULL)
    with open(self.fuzzer.results('zircon.log'), 'w') as log:
      self.host.zircon_tool(['loglistener'], logfile=log)

  def __exit__(self, e_type, e_value, traceback):
    """Gathers and processes the fuzzing logs.

      This will stop loglistener and symbolize its output.  It will also
      retrieve fuzzing logs from
      the device for each libFuzzer worker, and download any test unit artifacts
      they reference.
    """
    subprocess.call(['killall', 'loglistener'])
    with open(self.fuzzer.results('zircon.log'), 'r') as log_in:
      with open(self.fuzzer.results('symbolized.log'), 'w') as log_out:
        self.host.symbolize(log_in, log_out)
    try:
      self.device.fetch(
          self.fuzzer.data_path('fuzz-*.log'), self.fuzzer.results())
    except subprocess.CalledProcessError:
      pass
    units = []
    pattern = re.compile(r'Test unit written to (\S*)$')
    for log in os.listdir(self.fuzzer.results()):
      if log.startswith('fuzz-') and log.endswith('.log'):
        with open(self.fuzzer.results(log), 'r') as f:
          matches = [pattern.match(line) for line in f.readlines()]
          units.extend([m.group(1) for m in matches if m])
    for unit in units:
      self.device.fetch(unit, self.fuzzer.results())
