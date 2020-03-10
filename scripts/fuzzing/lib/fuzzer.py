#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import datetime
import errno
import glob
import os
import re
import subprocess
import time
import sys

from device import Device
from host import Host
from cliutils import show_menu


class Fuzzer(object):
    """Represents a Fuchsia fuzz target.

    This represents a binary fuzz target produced the Fuchsia build, referenced
    by a component manifest, and included in a fuzz package.  It provides an
    interface for running the fuzzer in different common modes, allowing
    specific command line arguments to libFuzzer to be abstracted.

    Attributes:
      device: A Device where this fuzzer can be run
      host: The build host that built the fuzzer
      pkg: The GN fuzzers_package name
      tgt: The GN fuzzers name
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
    def filter(cls, fuzzers, name):
        """Filters a list of fuzzer names.

      Takes a list of fuzzer names in the form `pkg`/`tgt` and a name to filter
      on.  If the name is of the form 'x/y', the filtered list will include all
      the fuzzer names where 'x' is a substring of `pkg` and y is a substring
      of `tgt`; otherwise it includes all the fuzzer names where `name` is a
      substring of either `pkg` or `tgt`.

      Returns:
        A list of fuzzer names matching the given name.

      Raises:
        FuzzerNameError: Name is malformed, e.g. of the form 'x/y/z'.
    """
        if not name or name == '':
            return fuzzers
        names = name.split('/')
        if len(names) == 2 and (names[0], names[1]) in fuzzers:
            return [(names[0], names[1])]
        if len(names) == 1:
            return list(
                set(Fuzzer.filter(fuzzers, '/' + name)) |
                set(Fuzzer.filter(fuzzers, name + '/')))
        elif len(names) != 2:
            raise Fuzzer.NameError('Malformed fuzzer name: ' + name)
        filtered = []
        for pkg, tgt in fuzzers:
            if names[0] in pkg and names[1] in tgt:
                filtered.append((pkg, tgt))
        return filtered

    @classmethod
    def from_args(cls, device, args):
        """Constructs a Fuzzer from command line arguments, showing a
        disambiguation menu if specified name matches more than one fuzzer."""

        matches = Fuzzer.filter(device.host.fuzzers, args.name)

        if not matches:
            sys.exit('No matching fuzzers found. Try `fx fuzz list`.')

        if len(matches) > 1:
            print('More than one match found, please pick one from the list:')
            choices = ["/".join(m) for m in matches]
            fuzzer_name = show_menu(choices).split('/')
        else:
            fuzzer_name = matches[0]

        return cls(device, fuzzer_name[0], fuzzer_name[1], args.output,
                   args.foreground, args.debug)

    def __init__(self, device, pkg, tgt, output=None, foreground=False, debug=False):
        self.device = device
        self.host = device.host
        self.pkg = pkg
        self.tgt = tgt
        if output:
            self._output = output
        else:
            self._output = self.host.join(
                'test_data', 'fuzzing', self.pkg, self.tgt)
        self._foreground = foreground
        self._debug = debug

    def __str__(self):
        return self.pkg + '/' + self.tgt

    def data_path(self, relpath=''):
        """Canonicalizes the location of mutable data for this fuzzer."""
        return '/data/r/sys/fuchsia.com:%s:0#meta:%s.cmx/%s' % (
            self.pkg, self.tgt, relpath)

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
            lines = self.device.ls(self.data_path())
            for file, _ in lines.iteritems():
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

    def results(self, relpath=None):
        """Returns the path in the previously prepared results directory."""
        if relpath:
            return os.path.join(self._output, 'latest', relpath)
        else:
            return os.path.join(self._output, 'latest')

    def url(self):
        return 'fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx' % (self.pkg, self.tgt)

    def _create(self, fuzzer_args, logfile=None):
        # Disable exception handling in debug mode
        if self._debug:
            for signal in ['segv', 'bus', 'ill', 'fpe', 'abrt']:
                fuzzer_args.append("-handle_{}=0".format(signal))

        fuzz_cmd = ['run', self.url(), '-artifact_prefix=data/'] + fuzzer_args

        print('+ ' + ' '.join(fuzz_cmd))
        return self.device.ssh(fuzz_cmd)

    def start(self, fuzzer_args):
        """Runs the fuzzer.

      Executes a fuzzer in the "normal" fuzzing mode. If the fuzzer is being
      run in the foreground, it will block until the fuzzer exits. If the
      fuzzer is being run in the background, it will return immediately after
      the fuzzer has been started, and callers should subsequently call
      Fuzzer.monitor().

      The command will be like:
      run fuchsia-pkg://fuchsia.com/<pkg>#meta/<tgt>.cmx \
        -artifact_prefix=data/ -jobs=1 data/corpus/

      See also: https://llvm.org/docs/LibFuzzer.html#running

      Args:
        fuzzer_args: Command line arguments to pass to libFuzzer

      Returns:
        The fuzzer's process ID. May be 0 if the fuzzer stops immediately.
    """
        self.require_stopped()
        self.device.rm(self.data_path('fuzz-*.log'))
        results = os.path.join(
            self._output,
            datetime.datetime.today().isoformat())
        try:
            os.unlink(self.results())
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise
        try:
            os.makedirs(results)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise
        os.symlink(results, self.results())

        if len(filter(lambda x: x.startswith('-jobs='), fuzzer_args)) == 0:
            if self._foreground:
                fuzzer_args.append('-jobs=0')
            else:
                fuzzer_args.append('-jobs=1')
        self.device.ssh(['mkdir', '-p', self.data_path('corpus')]).check_call()
        if len(filter(lambda x: not x.startswith('-'), fuzzer_args)) == 0:
            fuzzer_args.append('data/corpus/')

        # Fuzzer logs are saved to fuzz-*.log when running in the background.
        # We tee the output to fuzz-0.log when running in the foreground to
        # make the rest of the plumbing look the same.
        cmd = self._create(fuzzer_args)
        if self._foreground:
            cmd.stderr = subprocess.PIPE
        proc = cmd.popen()
        if self._foreground:
            logfile = self.results('fuzz-0.log')
            with open(logfile, 'w') as fd_out:
                self.symbolize_log(proc.stderr, fd_out, echo=True)
            proc.wait()

    def symbolize_log(self, fd_in, fd_out, echo=False):
        """Constructs a symbolized fuzzer log from a device.

        Merges the provided fuzzer log with the symbolized system log for the
        fuzzer process.

        Args:
          fd_in: An object supporting readline(), such as a file or pipe.
          fd_out: An object supporting write(), such as a file.
          echo: If true, display text being written to fd_out.
        """
        pid = -1
        sym = None
        artifacts = []
        pid_pattern = re.compile(r'^==([0-9]+)==')
        mut_pattern = re.compile(r'^MS: [0-9]*')  # Fuzzer::DumpCurrentUnit
        art_pattern = re.compile(r'Test unit written to data/(\S*)')
        for line in iter(fd_in.readline, ''):
            pid_match = pid_pattern.search(line)
            mut_match = mut_pattern.search(line)
            art_match = art_pattern.search(line)
            if pid_match:
                pid = int(pid_match.group(1))
            if mut_match:
                if pid <= 0:
                    pid = self.device.guess_pid()
                if not sym:
                    raw = self.device.dump_log(['--pid', str(pid)])
                    sym = self.host.symbolize(raw)
                    fd_out.write(sym)
                    if echo:
                        print(sym.strip())
            if art_match:
                artifacts.append(art_match.group(1))
            fd_out.write(line)
            if echo:
                print(line.strip())
        for artifact in artifacts:
            self.device.fetch(self.data_path(artifact), self.results())

    def monitor(self):
        """Waits for a fuzzer to complete and symbolizes its logs.

        Polls the device to determine when the fuzzer stops. Retrieves,
        combines and symbolizes the associated fuzzer and kernel logs. Fetches
        any referenced test artifacts, e.g. crashes.
        """
        while self.is_running():
            time.sleep(2)
        self.device.fetch(self.data_path('fuzz-*.log'), self.results())
        logs = glob.glob(self.results('fuzz-*.log'))
        artifacts = []
        for log in logs:
            tmp = log + '.tmp'
            with open(log, 'r') as fd_in:
                with open(tmp, 'w') as fd_out:
                    self.symbolize_log(fd_in, fd_out, echo=False)
            os.rename(tmp, log)

    def stop(self):
        """Stops any processes with a matching component manifest on the device."""
        pids = self.device.getpids()
        if self.tgt in pids:
            self.device.ssh(['kill', str(pids[self.tgt])]).check_call()

    def repro(self, fuzzer_args):
        """Runs the fuzzer with test input artifacts.

      Executes a command like:
      run fuchsia-pkg://fuchsia.com/<pkg>#meta/<tgt>.cmx \
        -artifact_prefix=data -jobs=1 data/<artifact>...

      If host artifact paths are specified, they will be copied to the device
      instance and used. Otherwise, the fuzzer will use all artifacts present
      on the device.

      See also: https://llvm.org/docs/LibFuzzer.html#options

      Returns: Number of test input artifacts found.
    """
        options = []
        artifacts = []
        for arg in fuzzer_args:
            if arg.startswith('-'):
                options.append(arg)
            elif os.path.exists(arg):
                artifact = os.path.basename(arg)
                self.device.store(arg, self.data_path())
                artifacts.append(artifact)
            else:
                print('File not found, skipping: ' + arg)
        if not artifacts:
            artifacts = self.list_artifacts()
        if not artifacts:
            return 0
        self._create(options + ['data/' + a for a in artifacts]).call()
        return len(artifacts)

    def merge(self, fuzzer_args):
        """Attempts to minimizes the fuzzer's corpus.

      Executes a command like:
      run fuchsia-pkg://fuchsia.com/<pkg>#meta/<tgt>.cmx \
        -artifact_prefix=data -jobs=1 \
        -merge=1 -merge_control_file=data/.mergefile \
        data/corpus/ data/corpus.prev/'

      See also: https://llvm.org/docs/LibFuzzer.html#corpus

      Returns: Same as measure_corpus
    """
        self.require_stopped()
        if self.measure_corpus() == (0, 0):
            return (0, 0)
        self.device.ssh(['mkdir', '-p', self.data_path('corpus')]).check_call()
        self.device.ssh(['mkdir', '-p',
                         self.data_path('corpus.prev')]).check_call()
        self.device.ssh(
            ['mv',
             self.data_path('corpus/*'),
             self.data_path('corpus.prev')]).check_call()
        # Save mergefile in case we are interrupted
        fuzzer_args = [
            '-merge=1', '-merge_control_file=data/.mergefile'
        ] + fuzzer_args
        fuzzer_args.append('data/corpus/')
        fuzzer_args.append('data/corpus.prev/')
        self._create(fuzzer_args).check_call()
        # Cleanup
        self.device.rm(self.data_path('.mergefile'))
        self.device.rm(self.data_path('corpus.prev'), recursive=True)
        return self.measure_corpus()
