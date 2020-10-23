# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""This module implements helpers for GN SDK e2e tests.
"""

# Note, this is run on bots, which only support python2.7.
# Be sure to only use python2.7 features in this module.

import os
import signal
import sys
import subprocess
from subprocess import Popen, PIPE

class popen:
  """Runs subprocess.Popen and returns the process object.

  This is meant to be used as a context manager. For example:

      with popen(['echo', 'hello']) as p:
        # Use p here

  This object ensures that any child processes spawned by the command
  are killed by forcing the subprocess to use a process group. This
  prevents e.g. the emulator from sticking around as a zombie process
  after the test is complete.

  Args:
    command -- The list of command line arguments.
  """
  def __init__(self, command):
    self._command = command
    self._process = None

  def __enter__(self):
    self._process = Popen(self._command, stdout=PIPE, stderr=PIPE,
                          close_fds=True, preexec_fn=os.setsid)
    return self._process

  def __exit__(self, type, value, traceback):
    if self._process.poll() is None:
      os.killpg(os.getpgid(self._process.pid), signal.SIGKILL)

