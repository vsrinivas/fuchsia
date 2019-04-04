#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import json
import os
import re
import subprocess


class Host(object):
  """Represents a local system with a build of Fuchsia.

    This class abstracts the details of various repository, tool, and build
    paths, as well as details about the host architecture and platform.

    Attributes:
      fuchsia: The root directory of the Fuchsia repository
      ssh_config: Location of the SSH configuration used to connect to device.
  """

  # Convenience file descriptor for silencing subprocess output
  DEVNULL = open(os.devnull, 'w')

  class ConfigError(RuntimeError):
    """Indicates the host is not configured for building Fuchsia."""
    pass

  @classmethod
  def join(cls, *segments):
    fuchsia = os.getenv('FUCHSIA_DIR')
    if not fuchsia:
      raise Host.ConfigError('Unable to find FUCHSIA_DIR; have you `fx set`?')
    return os.path.join(fuchsia, *segments)

  def __init__(self):
    build_dir = None
    with open(Host.join('.config'), 'r') as f:
      for line in f.readlines():
        m = re.search(r'^FUCHSIA_BUILD_DIR=\'(\S*)\'$', line)
        if m:
          build_dir = Host.join(m.group(1))
    if not build_dir:
      raise Host.ConfigError(
          'Unable to determine FUCHSIA_BUILD_DIR from .config')
    self.ssh_config = os.path.join(build_dir, 'ssh-keys', 'ssh_config')
    self._ids = os.path.join(build_dir, 'ids.txt')

    if os.uname()[0] == 'Darwin':
      self._platform = 'mac-x64'
    else:
      self._platform = 'linux-x64'

    self.fuzzers = []
    try:
      with open(os.path.join(build_dir, 'fuzzers.json')) as f:
        fuzz_specs = json.load(f)
      for fuzz_spec in fuzz_specs:
        pkg = fuzz_spec['fuzz_package']
        for tgt in fuzz_spec['fuzz_targets']:
          self.fuzzers.append((pkg, tgt))
    except IOError as e:
      if e.errno != errno.ENOENT:
        raise

  def zircon_tool(self, cmd, logfile=None):
    """Executes a tool found in the ZIROCN_BUILD_DIR."""
    cmd = [Host.join('out', 'build-zircon', 'tools', cmd[0])] + cmd[1:]
    if logfile:
      subprocess.Popen(cmd, stdout=logfile, stderr=subprocess.STDOUT)
    else:
      return subprocess.check_output(cmd, stderr=Host.DEVNULL).strip()
