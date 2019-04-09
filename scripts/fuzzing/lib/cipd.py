#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import argparse
import tempfile
import subprocess
import shutil

from lib.host import Host
from lib.fuzzer import Fuzzer


class Cipd(object):
  """Chrome Infra Package Deployer interface for Fuchsia fuzzing.

    Fuzzers in Fuchsia use CIPD to store and manage their corpora, which are
    sets of "interesting"
    inputs as determined by the individual fuzzer.  For example, see
    https://llvm.org/docs/LibFuzzer.html#corpus
    for details on how libFuzzer uses corpora.

    This class acts as a context manager to ensure temporary root directories
    are cleaned up.

    Attributes:
        root: Local directory where CIPD packages can be assembled or unpacked.
          If not specified by the command line arguments, this will be a
          temporary directory.
  """

  @classmethod
  def from_args(cls, fuzzer, args, label=None):
    """Constructs a Cipd from command line arguments."""
    return cls(fuzzer, args.no_cipd, args.staging, label)

  def __init__(self, fuzzer, disabled=False, root=None, label=None):
    self.disabled = disabled
    if disabled:
      return
    self.device = fuzzer.device
    self.fuzzer = fuzzer
    self._bin = Host.join('.jiri_root', 'bin', 'cipd')
    if root:
      self.root = root
      self._is_tmp = False
    else:
      self.root = tempfile.mkdtemp()
      self._is_tmp = True
    self.label = label

  def __enter__(self):
    return self

  def __exit__(self, e_type, e_value, traceback):
    if not self.disabled and self._is_tmp:
      shutil.rmtree(self.root)

  def _pkg(self):
    """Defines naming convention for Fuchsia fuzzing corpora in CIPD."""
    return 'fuchsia/test_data/fuzzing/' + str(self.fuzzer)

  def _exec(self, cmd, quiet=False):
    """Executes a CIPD command."""
    if quiet:
      subprocess.check_call([self._bin] + cmd, stdout=Host.DEVNULL)
    else:
      subprocess.check_call([self._bin] + cmd)

  def list(self, quiet=False):
    """Lists the instances of the corpus available for the fuzzer."""
    if self.disabled:
      return False
    try:
      self._exec(['instances', self._pkg()], quiet)
      return True
    except subprocess.CalledProcessError:
      print 'No corpus instances found in CIPD for ' + str(self.fuzzer)
      return False

  def install(self):
    """Downloads and unpacks a CIPD package for a Fuchsia fuzzer."""
    if self.disabled or not self.list(quiet=True):
      return False
    self.fuzzer.require_stopped()
    cipd_cmd = ['install', self._pkg()]
    if self.label:
      cipd_cmd.append(self.label)
    cipd_cmd.extend(['--root', self.root])
    self._exec(cipd_cmd)
    subprocess.check_call(['chmod', '-R', '+w', self.root])
    self.device.store(
        os.path.join(self.root, '*'), self.fuzzer.data_path('corpus'))
    return True

  def create(self):
    """Bundles and uploads a CIPD package for a Fuchsia fuzzer corpus."""
    if self.disabled:
      return False
    self.fuzzer.require_stopped()
    self.device.fetch(self.fuzzer.data_path('corpus/*'), self.root)
    pkg_def = os.path.join(self.root, 'cipd.yaml')
    elems = os.listdir(self.root)
    with open(pkg_def, 'w') as f:
      f.write('package: ' + self._pkg() + '\n')
      f.write('description: Auto-generated fuzzing corpus for ' +
              str(self.fuzzer) + '\n')
      f.write('install_mode: copy\n')
      f.write('data:\n')
      for elem in elems:
        if 'cipd' not in elem:
          f.write('  - file: ' + elem + '\n')
    try:
      self._exec(['create', '--pkg-def', pkg_def, '--ref', 'latest'])
      return True
    except subprocess.CalledProcessError:
      print('Failed to upload corpus for ' + str(self.fuzzer) +
            '; have you run \'cipd auth-login\'?')
      return False
