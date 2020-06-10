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
      package: The GN fuzzers_package name (or package_name).
      executable: The GN fuzzers name (or output_name).
  """

    # Matches the prefixes in libFuzzer passed to |Fuzzer::DumpCurrentUnit| or
    # |Fuzzer::WriteUnitToFileWithPrefix|.
    ARTIFACT_PREFIXES = [
        'crash', 'leak', 'mismatch', 'oom', 'slow-unit', 'timeout'
    ]

    DEBUG_OPTIONS = [
        'handle_segv', 'handle_bus', 'handle_ill', 'handle_fpe', 'handle_abrt'
    ]

    @classmethod
    def from_args(cls, device, args):
        """Constructs a Fuzzer from command line arguments, showing a
        disambiguation menu if specified name matches more than one fuzzer."""
        cli = device.host.cli
        matches = device.host.fuzzers(args.name)
        if not matches:
            sys.exit('No matching fuzzers found. Try `fx fuzz list`.')

        if len(matches) > 1:
            cli.echo(
                'More than one match found, please pick one from the list:')
            choices = ['/'.join(m) for m in matches]
            fuzzer_name = cli.choose(choices).split('/')
        else:
            fuzzer_name = matches[0]

        fuzzer = cls(device, fuzzer_name[0], fuzzer_name[1])
        if args.output:
            fuzzer.output = args.output
        fuzzer.foreground = args.foreground
        fuzzer.debug = args.debug
        return fuzzer

    def __init__(self, device, package, executable):
        self._device = device
        self._package = package
        self._executable = executable
        self._pid = None
        self._options = {'artifact_prefix': 'data/'}
        self._libfuzzer_args = []
        self._subprocess_args = []
        self._debug = False
        self._foreground = False
        self._output = device.host.fxpath(
            'local', '{}_{}'.format(self.package, self.executable))

    def __str__(self):
        return '{}/{}'.format(self.package, self.executable)

    @property
    def device(self):
        return self._device

    @property
    def package(self):
        return self._package

    @property
    def executable(self):
        return self._executable

    @property
    def libfuzzer_opts(self):
        return self._libfuzzer_opts

    @libfuzzer_opts.setter
    def libfuzzer_opts(self, libfuzzer_opts):
        self._libfuzzer_opts = libfuzzer_opts

    @property
    def libfuzzer_args(self):
        return self._libfuzzer_args

    @libfuzzer_args.setter
    def libfuzzer_args(self, libfuzzer_args):
        self._libfuzzer_args = libfuzzer_args

    @property
    def subprocess_args(self):
        return self._subprocess_args

    @subprocess_args.setter
    def subprocess_args(self, subprocess_args):
        self._subprocess_args = subprocess_args

    @property
    def output(self):
        return self._output

    @output.setter
    def output(self, output):
        if not output or not self.device.host.isdir(output):
            raise ValueError('Invalid output directory: {}'.format(output))
        self._output = output

    @property
    def foreground(self):
        return self._foreground

    @foreground.setter
    def foreground(self, foreground):
        self._foreground = foreground

    @property
    def debug(self):
        return self._debug

    @debug.setter
    def debug(self, debug):
        self._debug = debug

    def data_path(self, relpath=''):
        """Canonicalizes the location of mutable data for this fuzzer."""
        return '/data/r/sys/fuchsia.com:%s:0#meta:%s.cmx/%s' % (
            self.package, self.executable, relpath)

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

    def is_running(self, refresh=False):
        """Checks the device and returns whether the fuzzer is running.

           See the note about "refresh" on Device.getpid().
        """
        self._pid = self.device.getpid(
            self.package, self.executable, refresh=refresh)
        return self._pid > 0

    def require_stopped(self, refresh=False):
        """Raise an exception if the fuzzer is running."""
        if self.is_running(refresh=refresh):
            raise RuntimeError(
                str(self) + ' is running and must be stopped first.')

    def url(self):
        return 'fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx' % (
            self.package, self.executable)

    def _set_libfuzzer_args(self):
        """ Adds the corpus directory and argument."""
        if self._libfuzzer_args:
            arg = self._libfuzzer_args[0]
            if arg.startswith('-'):
                raise ValueError(
                    'Unrecognized flag \'{}\'; use -help=1 to list all flags'.
                    format(arg))
            else:
                raise ValueError(
                    'Unexpected argument: \'{}\'. Passing corpus arguments to libFuzzer '
                    +
                    'is unsupported due to namespacing; use `fx fuzz fetch` instead.'
                    .format(arg))
        self.device.ssh(['mkdir', '-p', self.data_path('corpus')]).check_call()
        self._libfuzzer_args = ['data/corpus/']

    def _create(self):
        if not self.foreground:
            self._options['jobs'] = '1'
        for option in Fuzzer.DEBUG_OPTIONS:
            if self.debug:
                self._options[option] = '0'
        self._options.update(self._libfuzzer_opts)
        fuzz_cmd = ['run', self.url()]
        for key, value in sorted(self._options.items()):
            fuzz_cmd.append('-{}={}'.format(key, value))
        fuzz_cmd += self._libfuzzer_args
        if self._subprocess_args:
            fuzz_cmd += ['--']
            fuzz_cmd += self._subprocess_args
        print('+ ' + ' '.join(fuzz_cmd))
        return self.device.ssh(fuzz_cmd)

    def start(self):
        """Runs the fuzzer.

      Executes a fuzzer in the "normal" fuzzing mode. If the fuzzer is being
      run in the foreground, it will block until the fuzzer exits. If the
      fuzzer is being run in the background, it will return immediately after
      the fuzzer has been started, and callers should subsequently call
      Fuzzer.monitor().

      The command will be like:
      run fuchsia-pkg://fuchsia.com/<pkg>#meta/<executable>.cmx \
        -artifact_prefix=data/ \
        -dict=pkg/data/<executable>/dictionary \
        data/corpus/

      See also: https://llvm.org/docs/LibFuzzer.html#running

      Returns:
        The fuzzer's process ID. May be 0 if the fuzzer stops immediately.
    """
        self.require_stopped()
        logs = self.data_path('fuzz-*.log')
        self.device.rm(logs)
        self.device.host.mkdir(self._output)

        self._options['dict'] = 'pkg/data/{}/dictionary'.format(self.executable)

        # Fuzzer logs are saved to fuzz-*.log when running in the background.
        # We tee the output to fuzz-0.log when running in the foreground to
        # make the rest of the plumbing look the same.
        self._set_libfuzzer_args()
        cmd = self._create()
        if self.foreground:
            cmd.stderr = subprocess.PIPE
        proc = cmd.popen()
        if self.foreground:
            self.symbolize_log(proc.stderr, 0, echo=True)
            proc.wait()

    def _logfile(self, job_num):
        """Returns the path to the symbolized log for a fuzzing job."""
        if not self._logbase:
            now = datetime.datetime.now().replace(microsecond=0)
            self._logbase = now.strftime('%Y-%m-%d-%H%M')
        logfile = 'fuzz-{}-{}.log'.format(self._logbase, job)
        return os.path.join(self._output, logfile)

    def symbolize_log(self, fd_in, job_num, echo=False):
        """Constructs a symbolized fuzzer log from a device.

        Merges the provided fuzzer log with the symbolized system log for the
        fuzzer process. One each of fd_in/filename_in and fd_out/filename_out is
        required.

        This method is overridden when testing.

        Args:
          fd_in:        An object supporting readline(), such as a file or pipe.
          filename_in:  A path to a file to write.
          echo:         If true, display text being written to fd_out.
        """
        filename_out = self._logfile(job_num)
        with open(filename_out, 'w') as fd_out:
            return self._symbolize_log_impl(fd_in, fd_out, echo)
        self.device.host.link(
            filename_out, os.path.join(self.output, 'fuzz-latest.log'))

    def _symbolize_log_impl(self, fd_in, fd_out, echo):
        """Implementation of symbolize_log that takes file-like objects."""
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
                    sym = self.device.host.symbolize(raw)
                    fd_out.write(sym)
                    if echo:
                        print(sym.strip())
            if art_match:
                artifacts.append(art_match.group(1))
            fd_out.write(line)
            if echo:
                print(line.strip())
        for artifact in artifacts:
            self.device.fetch(self.data_path(artifact), self.output)
        return sym != None

    def monitor(self):
        """Waits for a fuzzer to complete and symbolizes its logs.

        Polls the device to determine when the fuzzer stops. Retrieves,
        combines and symbolizes the associated fuzzer and kernel logs. Fetches
        any referenced test artifacts, e.g. crashes.
        """
        while self.is_running(refresh=True):
            time.sleep(2)

        logs = self.data_path('fuzz-*.log')
        self.device.fetch(logs, self._output, retries=2)
        self.device.rm(logs)

        logs = sorted(glob.glob(os.path.join(self.output, 'fuzz-*.log')))
        for job_num, log in enumerate(logs):
            with open(log) as fd_in:
                self.symbolize_log(fd_in, job_num, echo=False)
            self.device.host.rm(log)

    def stop(self):
        """Stops any processes with a matching component manifest on the device."""
        if self.is_running():
            self.device.ssh(['kill', str(self._pid)]).check_call()

    def repro(self):
        """Runs the fuzzer with test input artifacts.

      Executes a command like:
      run fuchsia-pkg://fuchsia.com/<package>#meta/<executable>.cmx \
        -artifact_prefix=data -jobs=1 data/<artifact>...

      If host artifact paths are specified, they will be copied to the device
      instance and used. Otherwise, the fuzzer will use all artifacts present
      on the device.

      See also: https://llvm.org/docs/LibFuzzer.html#options

      Returns: Number of test input artifacts found.
    """
        # If no files provided, use artifacts on device.
        if not self._libfuzzer_args:
            self._libfuzzer_args = self.list_artifacts()
        else:
            for arg in self._libfuzzer_args:
                self.device.store(arg, self.data_path())
        if not self._libfuzzer_args:
            return 0

        namespaced = []
        for arg in self._libfuzzer_args:
            namespaced.append(os.path.join('data', os.path.basename(arg)))
        self._libfuzzer_args = namespaced

        # Default to repro-ing in the foreground
        self.foreground = True

        self._create().call()
        return len(self._libfuzzer_args)
