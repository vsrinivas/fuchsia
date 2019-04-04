#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import datetime
import errno
import os
import subprocess
import time

from device import Device
from host import Host
from log import Log


class Fuzzer(object):
  """Represents a Fuchsia fuzz target.

    This represents a binary fuzz target produced the Fuchsia build, referenced
    by a component
    manifest, and included in a fuzz package.  It provides an interface for
    running the fuzzer in
    different common modes, allowing specific command line arguments to
    libFuzzer to be abstracted.

    Attributes:
      device: A Device where this fuzzer can be run
      host: The build host that built the fuzzer
      pkg: The GN fuzz_package name
      tgt: The GN fuzz_target name
  """

  # Matches the prefixes in libFuzzer passed to |Fuzzer::DumpCurrentUnit| or
  # |Fuzzer::WriteUnitToFileWithPrefix|.
  ARTIFACT_PREFIXES = [
      'crash', 'leak', 'mismatch', 'oom', 'slow-unit', 'timeout'
  ]

  class NameError(ValueError):
    """Indicates a supplied name is malformed or unusable."""
    pass

  class StateError(ValueError):
    """Indicates a command isn't valid for the fuzzer in its current state."""
    pass

  @classmethod
  def make_parser(cls, description, name_required=True):
    """Registers the command line arguments understood by Fuzzer."""
    parser = Device.make_parser(description)
    parser.add_argument(
        '-n',
        '--name',
        action='store',
        required=name_required,
        help='Fuzzer name to match.  This can be part of the package and/or' +
        ' target name, e.g. "foo", "bar", and "foo/bar" all match' +
        ' "foo_package/bar_target".')
    return parser

  @classmethod
  def filter(cls, fuzzers, name):
    """Filters a list of fuzzer names.

      Takes a list of fuzzer names in the form `pkg`/`tgt` and a name to filter
      on.  If the name is of the form 'x/y', the filtered list will include all
      the fuzzer names where x is a substring of `pkg` and y is a substring of
      `tgt`; otherwise it includes all the fuzzer names where `name` is a
      substring of either `pkg` or `tgt`.

      Returns:
        A list of fuzzer names matching the given name.

      Raises:
        FuzzerNameError: Name is malformed, e.g. of the form 'x/y/z'.
    """
    if not name or name == '':
      return fuzzers
    names = name.split('/')
    if len(names) == 1:
      return list(
          set(Fuzzer.filter(fuzzers, '/' + name))
          | set(Fuzzer.filter(fuzzers, name + '/')))
    elif len(names) != 2:
      raise Fuzzer.NameError('Malformed fuzzer name: ' + name)
    filtered = []
    for pkg, tgt in fuzzers:
      if names[0] in pkg and names[1] in tgt:
        filtered.append((pkg, tgt))
    return filtered

  @classmethod
  def from_args(cls, device, args):
    """Constructs a Fuzzer from command line arguments."""
    fuzzers = Fuzzer.filter(device.host.fuzzers, args.name)
    if len(fuzzers) != 1:
      raise Fuzzer.NameError('Name did not resolve to exactly one fuzzer: \'' +
                             args.name + '\'. Try using \'list-fuzzers\'.')
    return cls(device, fuzzers[0][0], fuzzers[0][1])

  def __init__(self, device, pkg, tgt):
    self.device = device
    self.host = device.host
    self.pkg = pkg
    self.tgt = tgt
    self._result_dir = None

  def __str__(self):
    return self.pkg + '/' + self.tgt

  def data_path(self, relpath=''):
    """Canonicalizes the location of mutable data for this fuzzer."""
    return '/data/r/sys/fuchsia.com:%s:0#meta:%s.cmx/%s' % (self.pkg, self.tgt,
                                                            relpath)

  def measure_corpus(self):
    """Returns the number of corpus elements and corpus size as a pair."""
    try:
      sizes = self.device.ls(self.data_path('corpus'))
      return (len(sizes), sum(sizes.values()))
    except subprocess.CalledProcessError:
      return (0, 0)

  def list_artifacts(self):
    """Returns a list of test unit artifacts, i.e. fuzzing crashes."""
    artifacts = []
    try:
      for file in [line[0] for line in self.device.ls(self.data_path())]:
        for prefix in Fuzzer.ARTIFACT_PREFIXES:
          if file.startswith(prefix):
            artifacts.append(file)
      return artifacts
    except subprocess.CalledProcessError:
      return []

  def is_running(self):
    """Checks the device and returns whether the fuzzer is running."""
    return self.tgt in self.device.getpids()

  def require_stopped(self):
    """Raise an exception if the fuzzer is running."""
    if self.is_running():
      raise Fuzzer.StateError(
          str(self) + ' is running and must be stopped first.')

  def prepare(self, base_dir=None):
    """Prepares a local directory to hold the fuzzing results."""
    self.require_stopped()
    if not base_dir:
      base_dir = Host.join()
    target_dir = os.path.join(base_dir, 'test_data', 'fuzzing', self.pkg,
                              self.tgt)
    result_dir = os.path.join(target_dir,
                              datetime.datetime.utcnow().isoformat())
    latest_dir = os.path.join(target_dir, 'latest')
    try:
      os.unlink(latest_dir)
    except OSError as e:
      if e.errno != errno.ENOENT:
        raise
    try:
      os.makedirs(result_dir)
    except OSError as e:
      if e.errno != errno.EEXIST:
        raise
    os.symlink(result_dir, latest_dir)
    self._result_dir = result_dir

  def results(self, relpath=''):
    """Returns the path in the previously prepared results directory."""
    return os.path.join(self._result_dir, relpath)

  def start(self, fuzzer_args, verbose=False):
    """Runs the fuzzer.

      Executes a fuzzer in the "normal" fuzzing mode. It creates a log context,
      and waits after
      spawning the fuzzer until it completes. As a result, callers will
      typically want to run this
      in a background process.

      Args:
        fuzzer_args: Command line arguments to pass to libFuzzer
        verbose: If true, tee libFuzzer output to stdout and a local log file.
    """
    if not self._result_dir:
      raise Fuzzer.StateError(str(self) + ' has not been prepared')
    with Log(self):
      fuzzer_cmd=['fuzz', 'start', str(self)]
      logfile=None
      if verbose:
        fuzzer_cmd.append('-jobs=0')
        logfile=self.results('fuzz-0.log')
      self.device.ssh(fuzzer_cmd + fuzzer_args, quiet=False, logfile=logfile)
      while self.is_running():
        time.sleep(2)

  def stop(self):
    """Stops any processes with a matching component manifest on the device."""
    pids = self.device.getpids()
    if self.tgt in pids:
      self.device.ssh(['kill', str(pids[self.tgt])])

  def repro(self, fuzzer_args):
    """Runs the fuzzer on previously found test unit artifacts."""
    self.require_stopped()
    self.device.ssh(['fuzz', 'repro', str(self)] + fuzzer_args, quiet=False)

