#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import json
import os
import re
import subprocess

from process import Process


class Host(object):
    """Represents a local system with a build of Fuchsia.

    This class abstracts the details of various repository, tool, and build
    paths, as well as details about the host architecture and platform.

    Attributes:
      cli:              The associated CommandLineInterface object.
      build_dir:        Path to the Fuchsia build output.
      symbolizer_exec:  Path to the Fuchsia symbolizer executable.
      llvm_symbolizer:  Path to the LLVM/Clang symbolizer library.
      build_id_dirs:    List of paths to symbolizer debug symbols.
  """

    def __init__(self, cli, fuchsia_dir):
        assert cli, 'CLI not set.'
        if not fuchsia_dir:
            cli.error(
                'FUCHSIA_DIR not set.', 'Have you sourced "scripts/fx-env.sh"?')
        self._fuchsia_dir = fuchsia_dir
        self._cli = cli
        self._build_dir = None
        self._symbolizer_exec = None
        self._llvm_symbolizer = None
        self._build_id_dirs = None
        self._gsutil = None
        self._fuzzers = []

    @property
    def cli(self):
        return self._cli

    @property
    def fuchsia_dir(self):
        return self._fuchsia_dir

    @property
    def build_dir(self):
        assert self._build_dir, 'Build directory not set'
        return self._build_dir

    @property
    def symbolizer_exec(self):
        assert self._symbolizer_exec, 'Symbolizer executable not set.'
        return self._symbolizer_exec

    @symbolizer_exec.setter
    def symbolizer_exec(self, symbolizer_exec):
        if not self.cli.isfile(symbolizer_exec):
            raise ValueError(
                'Invalid symbolizer executable: {}'.format(symbolizer_exec))
        self._symbolizer_exec = symbolizer_exec

    @property
    def llvm_symbolizer(self):
        assert self._llvm_symbolizer, 'LLVM symbolizer not set.'
        return self._llvm_symbolizer

    @llvm_symbolizer.setter
    def llvm_symbolizer(self, llvm_symbolizer):
        if not self.cli.isfile(llvm_symbolizer):
            raise ValueError(
                'Invalid LLVM symbolizer: {}'.format(llvm_symbolizer))
        self._llvm_symbolizer = llvm_symbolizer

    @property
    def build_id_dirs(self):
        assert self._build_id_dirs, 'Build ID directories not set.'
        return self._build_id_dirs

    @build_id_dirs.setter
    def build_id_dirs(self, build_id_dirs):
        for build_id_dir in build_id_dirs:
            if not self.cli.isdir(build_id_dir):
                raise ValueError(
                    'Invalid build ID directory: {}'.format(build_id_dir))
        self._build_id_dirs = build_id_dirs

    @property
    def gsutil(self):
        if not self._gsutil:
            try:
                self._gsutil = self.create_process(['which',
                                                    'gsutil']).check_output()
            except subprocess.CalledProcessError:
                self.cli.error(
                    'Unable to find gsutil.',
                    'Try installing the Google Cloud SDK.')
        return self._gsutil

    @gsutil.setter
    def gsutil(self, gsutil):
        if not self.cli.isfile(gsutil):
            raise ValueError('Invalid GS utility: {}'.format(gsutil))
        self._gsutil = gsutil

    # Initialization routines

    def configure(self, build_dir):
        """Sets multiple properties based on the given build directory."""
        self._build_dir = self.fxpath(build_dir)
        clang_dir = os.path.join(
            'prebuilt', 'third_party', 'clang', self.cli.platform)
        self.symbolizer_exec = self.fxpath(build_dir, 'host_x64', 'symbolize')
        self.llvm_symbolizer = self.fxpath(clang_dir, 'bin', 'llvm-symbolizer')
        self.build_id_dirs = [
            self.fxpath(clang_dir, 'lib', 'debug', '.build-id'),
            self.fxpath(build_dir, '.build-id'),
            self.fxpath(build_dir + '.zircon', '.build-id'),
        ]

    def read_fuzzers(self, pathname):
        """Parses the available fuzzers from an fuzzers.json pathname."""
        with self.cli.open(pathname, on_error=[
                'Failed to read fuzzers from {}.'.format(pathname),
                'Have you run "fx set ... --fuzz-with <sanitizer>"?'
        ]) as opened:
            fuzz_specs = json.load(opened)
        for fuzz_spec in fuzz_specs:
            package = fuzz_spec['fuzzers_package']
            for executable in fuzz_spec['fuzzers']:
                self._fuzzers.append((package, executable))

    def fuzzers(self, name=None):
        """Returns a (possibly filtered) list of fuzzer names.

        Takes a list of fuzzer names in the form `package`/`executable` and a name to filter
        on.  If the name is of the form 'x/y', the filtered list will include all
        the fuzzer names where 'x' is a substring of `package` and y is a substring
        of `executable`; otherwise it includes all the fuzzer names where `name` is a
        substring of either `package` or `executable`.

        Returns:
            A list of fuzzer names matching the given name.

        Raises:
            ValueError: Name is malformed, e.g. of the form 'x/y/z'.
    """
        if not name or name == '':
            return self._fuzzers
        names = name.split('/')
        if len(names) == 2 and (names[0], names[1]) in self._fuzzers:
            return [(names[0], names[1])]
        if len(names) == 1:
            as_package = set(self.fuzzers(name + '/'))
            as_executable = set(self.fuzzers('/' + name))
            return sorted(list(as_package | as_executable))
        elif len(names) != 2:
            raise ValueError('Malformed fuzzer name: ' + name)
        filtered = []
        for package, executable in self._fuzzers:
            if names[0] in package and names[1] in executable:
                filtered.append((package, executable))
        return sorted(filtered)

    # Other routines

    def fxpath(self, *segments):
        joined = os.path.join(*segments)
        if not joined.startswith(self.fuchsia_dir):
            joined = os.path.join(self.fuchsia_dir, joined)
        return joined

    def find_device(self, device_name=None, device_file=None):
        """Returns the IPv6 address for a device."""
        assert (not device_name or not device_file)
        if not device_name and device_file:
            device_name = device_file.read().strip()
        cmd = [self.fxpath('.jiri_root', 'bin', 'fx'), 'device-finder']
        if device_name:
            cmd += ['resolve', '-device-limit', '1', device_name]
        else:
            cmd += ['list']
        addrs = self.cli.create_process(cmd).check_output().strip()
        if not addrs:
            self.cli.error('Unable to find device.', 'Try "fx set-device".')
        addrs = addrs.split('\n')
        if len(addrs) != 1:
            self.cli.error('Multiple devices found.', 'Try "fx set-device".')
        return addrs[0]

    def symbolize(self, raw):
        """Symbolizes backtraces in a log file using the current build.

        Attributes:
            raw: Bytes representing unsymbolized lines.

        Returns:
            Bytes representing symbolized lines.
        """
        cmd = [self.symbolizer_exec, '-llvm-symbolizer', self.llvm_symbolizer]
        for build_id_dir in self.build_id_dirs:
            cmd += ['-build-id-dir', build_id_dir]
        p = self.cli.create_process(cmd)
        p.stdin = subprocess.PIPE
        p.stdout = subprocess.PIPE
        proc = p.popen()
        out, _ = proc.communicate(raw)
        if proc.returncode != 0:
            out = ''
        return re.sub(r'[0-9\[\]\.]*\[klog\] INFO: ', '', out)
