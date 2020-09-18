#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import json
import os
import re
import subprocess
from collections import defaultdict

from fuzzer import Fuzzer
from process import Process


class BuildEnv(object):
    """Represents a local build environment for Fuchsia.

    This class abstracts various repository, tool, and build details.

    Attributes:
      fuchsia_dir:      Path to Fuchsia source checkout.
      cli:              Associated CLI object.
      build_dir:        Path to the Fuchsia build output.
      symbolizer_exec:  Path to the Fuchsia symbolizer executable.
      llvm_symbolizer:  Path to the LLVM/Clang symbolizer library.
      build_id_dirs:    List of paths to symbolizer debug symbols.
      gsutil:           Path to the Google Cloud Storage utility.
  """

    def __init__(self, factory):
        assert factory, 'Factory not set.'
        self._factory = factory
        fuchsia_dir = self.host.getenv('FUCHSIA_DIR')
        if not fuchsia_dir:
            self.host.error(
                'FUCHSIA_DIR not set.', 'Have you sourced "scripts/fx-env.sh"?')
        self._fuchsia_dir = fuchsia_dir
        self._build_dir = None
        self._symbolizer_exec = None
        self._llvm_symbolizer = None
        self._build_id_dirs = None
        self._gsutil = None
        self._fuzzers = []

    @property
    def fuchsia_dir(self):
        return self._fuchsia_dir

    @property
    def host(self):
        return self._factory.host

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
        if not self.host.isfile(symbolizer_exec):
            self.host.error(
                'Invalid symbolizer executable: {}'.format(symbolizer_exec))
        self._symbolizer_exec = symbolizer_exec

    @property
    def llvm_symbolizer(self):
        assert self._llvm_symbolizer, 'LLVM symbolizer not set.'
        return self._llvm_symbolizer

    @llvm_symbolizer.setter
    def llvm_symbolizer(self, llvm_symbolizer):
        if not self.host.isfile(llvm_symbolizer):
            self.host.error(
                'Invalid LLVM symbolizer: {}'.format(llvm_symbolizer))
        self._llvm_symbolizer = llvm_symbolizer

    @property
    def build_id_dirs(self):
        assert self._build_id_dirs, 'Build ID directories not set.'
        return self._build_id_dirs

    @build_id_dirs.setter
    def build_id_dirs(self, build_id_dirs):
        for build_id_dir in build_id_dirs:
            if not self.host.isdir(build_id_dir):
                self.host.error(
                    'Invalid build ID directory: {}'.format(build_id_dir))
        self._build_id_dirs = build_id_dirs

    @property
    def gsutil(self):
        if not self._gsutil:
            try:
                self._gsutil = self.create_process(['which',
                                                    'gsutil']).check_output()
            except subprocess.CalledProcessError:
                self.host.error(
                    'Unable to find gsutil.',
                    'Try installing the Google Cloud SDK.')
        return self._gsutil

    @gsutil.setter
    def gsutil(self, gsutil):
        if not self.host.isfile(gsutil):
            self.host.error('Invalid GS utility: {}'.format(gsutil))
        self._gsutil = gsutil

    # Initialization routines

    def configure(self, build_dir):
        """Sets multiple properties based on the given build directory."""
        self._build_dir = self.path(build_dir)
        clang_dir = os.path.join(
            'prebuilt', 'third_party', 'clang', self.host.platform)
        self.symbolizer_exec = self.path(build_dir, 'host_x64', 'symbolize')
        self.llvm_symbolizer = self.path(clang_dir, 'bin', 'llvm-symbolizer')
        self.build_id_dirs = [
            self.path(clang_dir, 'lib', 'debug', '.build-id'),
            self.path(build_dir, '.build-id'),
            self.path(build_dir + '.zircon', '.build-id'),
        ]

    def read_fuzzers(self, pathname):
        """Parses the available fuzzers from an fuzzers.json pathname."""
        with self.host.open(pathname, on_error=[
                'Failed to read fuzzers from {}.'.format(pathname),
                'Have you run "fx set ... --fuzz-with <sanitizer>"?'
        ]) as opened:
            metadata = json.load(opened)

        fuzz_specs = []
        by_label = defaultdict(dict)
        for entry in metadata:
            # Try v2 metadata first.
            label = entry.get('label')
            if label:
                by_label[label].update(entry)
                continue
            # Fallback to v1 metadata.
            package = entry['fuzzers_package']
            package_url = 'fuchsia-pkg://fuchsia.com/{}'.format(package)
            for fuzzer in entry['fuzzers']:
                fuzz_specs.append(
                    {
                        'package': package,
                        'package_url': package_url,
                        'fuzzer': fuzzer,
                        'manifest': '{}.cmx'.format(fuzzer),
                        'label': '//generated/{}:{}'.format(package, fuzzer),
                    })
        fuzz_specs += by_label.values()
        self._fuzzers = [
            Fuzzer(self._factory, fuzz_spec) for fuzz_spec in fuzz_specs
        ]
        self._fuzzers.sort()

    def fuzzers(self, name=None, include_tests=False):
        """Returns a (possibly filtered) list of fuzzers.

        Matches the given name against the fuzzers previously instantiated by `read_fuzzers`.

        Parameters:
            name            A name to filter on. If the name is an exact match, it will return a
                            list containing the matching fuzzer. If the name is of the form 'x/y',
                            the filtered list will include all the fuzzers where 'x' is a substring
                            of `package` and y is a substring of `executable`; otherwise it includes
                            all the fuzzers where `name` is a substring of either `package` or
                            `executable`. If blank or omitted, all fuzzers are returned.
            include_tests   A boolean flag indicating whether to include fuzzer tests as fuzzers.
                            This can be useful for commands which only act on the source tree
                            without regard to a fuzzer being deployed on a device.

        Returns:
            A list of fuzzers matching the given name.

        Raises:
            ValueError: Name is malformed, e.g. of the form 'x/y/z'.
    """
        fuzzers = [
            fuzzer for fuzzer in self._fuzzers
            if fuzzer.matches(name) and (include_tests or not fuzzer.is_test)
        ]
        for fuzzer in fuzzers:
            if name == str(fuzzer):
                return [fuzzer]
        return fuzzers

    def fuzzer_tests(self, name=None):
        """Returns a (possibly filtered) list of fuzzer tests.

        Like fuzzers(), but returns uninstrumented fuzzer_tests instead of instrumented fuzzers.

        Parameters:
            name    A name to filter on. If the name is an exact match, it will return a list
                    containing the matching fuzzer test. If the name is of the form 'x/y', the
                    filtered list will include all the fuzzer tests where 'x' is a substring of
                    `package` and y is a substring of `executable` + '_test'; otherwise it includes
                    all the fuzzer tests where `name` is a substring of either `package` or
                    `executable` + '_test'. If blank or omitted, all fuzzer tests are returned.

        Returns:
            A list of fuzzer tests matching the given name.

        Raises:
            ValueError: Name is malformed, e.g. of the form 'x/y/z'.
    """
        fuzzer_tests = [
            fuzzer for fuzzer in self._fuzzers
            if fuzzer.matches(name) and fuzzer.is_test
        ]
        for fuzzer in fuzzer_tests:
            if name == str(fuzzer):
                return [fuzzer]
        return fuzzer_tests

    # Other routines

    def path(self, *segments):
        """Returns absolute path to a path in the build environment."""
        assert segments, 'No path segments provided.'
        joined = os.path.join(*segments)
        if joined.startswith('//'):
            joined = os.path.join(self.fuchsia_dir, joined[2:])
        elif not joined.startswith(self.fuchsia_dir):
            joined = os.path.join(self.fuchsia_dir, joined)
        return joined

    def find_device(self, device_name=None):
        """Returns the IPv6 address for a device."""
        cmd = [self.path('.jiri_root', 'bin', 'fx'), 'device-finder']
        if device_name:
            cmd += ['resolve', '-device-limit', '1', device_name]
        else:
            cmd += ['list']
        addrs = self.host.create_process(cmd).check_output().strip()
        if not addrs:
            self.host.error('Unable to find device.', 'Try "fx set-device".')
        addrs = addrs.split('\n')
        if len(addrs) != 1:
            self.host.error('Multiple devices found.', 'Try "fx set-device".')
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
        process = self.host.create_process(cmd)
        process.stdin = subprocess.PIPE
        process.stdout = subprocess.PIPE
        popened = process.popen()
        out, _ = popened.communicate(raw)
        if popened.returncode != 0:
            out = ''
        return re.sub(r'[0-9\[\]\.]*\[klog\] INFO: ', '', out)
