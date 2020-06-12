#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import datetime
import os
import re
import subprocess

from corpus import Corpus
from dictionary import Dictionary
from namespace import Namespace

from corpus import Corpus
from dictionary import Dictionary
from namespace import Namespace


class Fuzzer(object):
    """Represents a Fuchsia fuzz target.

    This represents a binary fuzz target produced the Fuchsia build, referenced
    by a component manifest, and included in a fuzz package.  It provides an
    interface for running the fuzzer in different common modes, allowing
    specific command line arguments to libFuzzer to be abstracted.

    Attributes:
      device:           The associated Device object.
      buildenv:         Alias for device.buildenv.
      cli:              Alias for device.buildenv.cli.
      package:          The GN fuzzers_package name (or package_name).
      executable:       The GN fuzzers name (or output_name).
      libfuzzer_opts:   "-key=val" options to pass to libFuzzer
      libfuzzer_inputs: Additional files and directories to pass to libFuzzer
      subprocess_args:  Additional arguments to pass to the fuzzer process.
      output:           Path under which to write the results of fuzzing.
      foreground:       Flag indicating whether to echo output.
      debug:            Flag indicating whether to allow a debugger to attach.
  """

    # Matches the prefixes in libFuzzer passed to |Fuzzer::DumpCurrentUnit| or
    # |Fuzzer::WriteUnitToFileWithPrefix|.
    ARTIFACT_PREFIXES = [
        'crash', 'leak', 'mismatch', 'oom', 'slow-unit', 'timeout'
    ]

    # Matches the options that cause libFuzzer to attach to the fuzzing process
    # as a debugger
    DEBUG_OPTIONS = [
        'handle_segv', 'handle_bus', 'handle_ill', 'handle_fpe', 'handle_abrt'
    ]

    def __init__(self, device, package, executable):
        assert device, 'Fuzzer device not set.'
        assert package, 'Fuzzer package name not set.'
        assert executable, 'Fuzzer executable name not set.'
        self._device = device
        self._package = package
        self._executable = executable
        self._pid = None
        self._options = {'artifact_prefix': 'data/'}
        self._libfuzzer_opts = {}
        self._libfuzzer_inputs = []
        self._subprocess_args = []
        self._debug = False
        self._foreground = False
        self._ns = Namespace(self)
        self._corpus = Corpus(self)
        self._dictionary = Dictionary(self)
        self._output = self.buildenv.path(
            'local', '{}_{}'.format(package, executable))
        self._logbase = None

    def __str__(self):
        return '{}/{}'.format(self.package, self.executable)

    @property
    def device(self):
        """The associated Device object."""
        return self._device

    @property
    def buildenv(self):
        """Alias for device.buildenv."""
        return self.device.buildenv

    @property
    def cli(self):
        """Alias for device.buildenv.cli."""
        return self.device.buildenv.cli

    @property
    def package(self):
        """The GN fuzzers_package name (or package_name)."""
        return self._package

    @property
    def executable(self):
        """The GN fuzzers name (or output_name)."""
        return self._executable

    @property
    def libfuzzer_opts(self):
        """"-key=val" options to pass to libFuzzer"""
        return self._libfuzzer_opts

    @libfuzzer_opts.setter
    def libfuzzer_opts(self, libfuzzer_opts):
        self._libfuzzer_opts = libfuzzer_opts

    @property
    def libfuzzer_inputs(self):
        """Additional files and directories to pass to libFuzzer"""
        return self._libfuzzer_inputs

    @libfuzzer_inputs.setter
    def libfuzzer_inputs(self, libfuzzer_inputs):
        self._libfuzzer_inputs = libfuzzer_inputs

    @property
    def subprocess_args(self):
        """Additional arguments to pass to the fuzzer process."""
        return self._subprocess_args

    @subprocess_args.setter
    def subprocess_args(self, subprocess_args):
        self._subprocess_args = subprocess_args

    @property
    def ns(self):
        return self._ns

    @property
    def corpus(self):
        return self._corpus

    @property
    def dictionary(self):
        return self._dictionary

    @property
    def output(self):
        """Path under which to write the results of fuzzing."""
        return self._output

    @output.setter
    def output(self, output):
        if not output or not self.cli.isdir(output):
            raise ValueError('Invalid output directory: {}'.format(output))
        self._output = output

    @property
    def foreground(self):
        """Flag indicating whether to echo output."""
        return self._foreground

    @foreground.setter
    def foreground(self, foreground):
        self._foreground = foreground

    @property
    def debug(self):
        """Flag indicating whether to allow a debugger to attach."""
        return self._debug

    @debug.setter
    def debug(self, debug):
        self._debug = debug

    def update(self, items):
        for key, val in vars(Fuzzer).items():
            if key in items and isinstance(val, property):
                setattr(self, key, items[key])

    def list_artifacts(self):
        """Returns a list of test unit artifacts in the output directory."""
        artifacts = []
        for prefix in Fuzzer.ARTIFACT_PREFIXES:
            artifacts += self.cli.glob(
                os.path.join(self.output, '{}-*'.format(prefix)))
        return artifacts

    def is_running(self, refresh=False):
        """Checks the device and returns whether the fuzzer is running.

           See the note about "refresh" on Device.getpid().
        """
        self._pid = self.device.getpid(
            self.package, self.executable, refresh=refresh)
        return self._pid > 0

    def require_stopped(self):
        """Raise an exception if the fuzzer is running."""
        if self.is_running(refresh=True):
            self.cli.error(
                '{} is running and must be stopped first.'.format(self))

    def url(self):
        return 'fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx' % (
            self.package, self.executable)

    def _launch(self):
        """Launches and returns a running fuzzer process."""
        if not self.foreground:
            self._options['jobs'] = '1'
        for option in Fuzzer.DEBUG_OPTIONS:
            if self.debug:
                self._options[option] = '0'
        self._options.update(self._libfuzzer_opts)
        fuzz_cmd = ['run', self.url()]
        for key, value in sorted(self._options.iteritems()):
            fuzz_cmd.append('-{}={}'.format(key, value))
        fuzz_cmd += self._libfuzzer_inputs
        if self._subprocess_args:
            fuzz_cmd += ['--']
            fuzz_cmd += self._subprocess_args

        # Start the process
        self.cli.mkdir(self.output)
        cmd = self.device.ssh(fuzz_cmd)
        if self.foreground:
            cmd.stderr = subprocess.PIPE
        proc = cmd.popen()
        if self.foreground:
            return proc

        # Wait for either the fuzzer to start and open its log, or exit.
        logs = self._logs()
        while proc.poll() == None and not self.ns.ls(logs):
            self.cli.sleep(0.5)
        if proc.returncode:
            self.cli.error('{} failed to start.'.format(self))
        return proc

    def _logs(self, pathname=None):
        """Returns a wildcarded path to the logs under pathname."""
        if pathname:
            assert self.cli.isdir(pathname), 'No such directory: {}'.format(
                pathname)
            return os.path.join(pathname, 'fuzz-*.log')
        else:
            return self.ns.data('fuzz-*.log')

    def logfile(self, job_num):
        """Returns the path to the symbolized log for a fuzzing job."""
        if not self._logbase:
            now = datetime.datetime.now().replace(microsecond=0)
            self._logbase = now.strftime('%Y-%m-%d-%H%M')
        logfile = 'fuzz-{}-{}.log'.format(self._logbase, job_num)
        return os.path.join(self._output, logfile)

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

        # Add dictionary
        self._options['dict'] = self.dictionary.nspath

        # Add corpus
        self._libfuzzer_inputs = self.corpus.inputs

        # Prep output
        logs = self._logs()
        self.ns.remove(logs)

        # When running in the foreground, interpret the output as coming from
        # fuzzing job 0. This makes the rest of the plumbing look the same.
        proc = self._launch()
        if self.foreground:
            self.symbolize_log(proc.stderr, 0, echo=True)
            proc.wait()

    def symbolize_log(self, fd_in, job_num, echo=False):
        """Constructs a symbolized fuzzer log from a device.

        Merges the provided fuzzer log with the symbolized system log for the
        fuzzer process.

        Args:
          fd_in:        An object supporting readline(), such as a file or pipe.
          job_num:      The job number of the corresponding fuzzing job.
          echo:         If true, display text being written to fd_out.
        """
        filename_out = self.logfile(job_num)
        with self.cli.open(filename_out, 'w') as fd_out:
            return self._symbolize_log_impl(fd_in, fd_out, echo)
        self.cli.link(
            filename_out, os.path.join(self.output, 'fuzz-latest.log'))

    def _symbolize_log_impl(self, fd_in, fd_out, echo):
        """Implementation of symbolize_log that takes file-like objects."""
        pid = -1
        sym = None
        artifacts = []
        pid_pattern = re.compile(r'^==([0-9]+)==')
        mut_pattern = re.compile(r'^MS: [0-9]*')  # Fuzzer::DumpCurrentUnit
        art_pattern = re.compile(r'Test unit written to (data/\S*)')
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
                    raw = self.device.dump_log('--pid', str(pid))
                    sym = self.buildenv.symbolize(raw)
                    fd_out.write(sym)
                    if echo:
                        self.cli.echo(sym.strip())
            if art_match:
                artifacts.append(art_match.group(1))
            fd_out.write(line)
            if echo:
                self.cli.echo(line.strip())
        self.ns.fetch(self.output, *artifacts)
        return sym != None

    def monitor(self):
        """Waits for a fuzzer to complete and symbolizes its logs.

        Polls the device to determine when the fuzzer stops. Retrieves,
        combines and symbolizes the associated fuzzer and kernel logs. Fetches
        any referenced test artifacts, e.g. crashes.
        """
        while self.is_running(refresh=True):
            self.cli.sleep(2)

        logs = self._logs()
        self.ns.fetch(self.output, logs)
        self.ns.remove(logs)

        logs = self._logs(self.output)
        logs = self.cli.glob(logs)
        for job_num, log in enumerate(logs):
            with self.cli.open(log) as fd_in:
                self.symbolize_log(fd_in, job_num, echo=False)
            self.cli.remove(log)

    def stop(self):
        """Stops any processes with a matching component manifest on the device."""
        if self.is_running():
            self.device.ssh(['kill', str(self._pid)]).check_call()

    def repro(self):
        """Runs the fuzzer with test input artifacts.

        Executes a command like:
        run fuchsia-pkg://fuchsia.com/<package>#meta/<executable>.cmx \
        -artifact_prefix=data -jobs=1 data/<artifact>...

        The specified artifacts will be copied to the device instance and used.

        See also: https://llvm.org/docs/LibFuzzer.html#options
        """
        if not self._libfuzzer_inputs:
            self.cli.error('No units provided.', 'Try "fx fuzz help".')

        # Device.store will glob patterns like 'crash-*'.
        self.require_stopped()
        self._libfuzzer_inputs = self.ns.store(
            self.ns.data(), *self._libfuzzer_inputs)

        # Default to repro-ing in the foreground
        self.foreground = True
        self._launch().wait()

    def analyze(self):
        """Collects coverage data for a finite amount of time."""
        self.require_stopped()

        # Add dictionary
        self._options['dict'] = self.dictionary.nspath

        # Add corpus
        self._libfuzzer_inputs = self.corpus.inputs

        # Run in the background for 1 minute, then print results.
        self._options['max_total_time'] = '60'
        self._options['print_coverage'] = '1'
        self.foreground = False
        proc = self._launch()

        self.cli.echo('Analyzing fuzzer...')
        delay = float(self._options['max_total_time']) / 79
        for i in range(79):
            self.cli.echo('\r[' + ('#' * i).ljust(78) + ']', end='')
            self.cli.sleep(delay)
        self.cli.echo('')
        proc.wait()
        self.monitor()
