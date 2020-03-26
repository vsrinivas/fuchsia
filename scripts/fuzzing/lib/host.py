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
      fuzzers:   The fuzzer binaries available in the current Fuchsia image
      build_dir: The build output directory, if present.
  """

    # Convenience file descriptor for silencing subprocess output
    DEVNULL = open(os.devnull, 'w')

    class ConfigError(RuntimeError):
        """Indicates the host is not configured for building Fuchsia."""
        pass

    @classmethod
    def from_build(cls):
        """Uses a local build directory to configure a Host object from it."""
        host = Host()
        host.set_build_dir(host.find_build_dir())
        return host

    @classmethod
    def join(cls, *segments):
        """Creates a source tree path."""
        fuchsia = os.getenv('FUCHSIA_DIR')
        if not fuchsia:
            raise Host.ConfigError(
                'Unable to find FUCHSIA_DIR; have you `fx set`?')
        return os.path.join(fuchsia, *segments)

    def __init__(self):
        self._ids = []
        self._llvm_symbolizer = None
        self._platform = 'mac-x64' if os.uname()[0] == 'Darwin' else 'linux-x64'
        self._symbolizer_exec = None
        self._zxtools = None
        self.fuzzers = []
        self.build_dir = None

    @classmethod
    def find_build_dir(self):
        """Examines the source tree to locate a build directory."""
        build_dir = Host.join('.fx-build-dir')
        if not os.path.exists(build_dir):
            raise Host.ConfigError(
                'Unable to find .fx-build-dir; have you `fx set`?')
        with open(build_dir, 'r') as f:
            return Host.join(f.read().strip())

    def get_host_out_dir(self):
      return Host.join(Host.find_build_dir(), 'host_x64')

    def add_build_ids(self, build_ids):
        """Sets the build IDs used to symbolize logs."""
        if os.path.exists(build_ids):
            self._ids.append(build_ids)

    def set_zxtools(self, zxtools):
        """Sets the location of the Zircon host tools directory."""
        if not os.path.isdir(zxtools):
            raise Host.ConfigError('Unable to find Zircon host tools.')
        self._zxtools = zxtools

    def set_symbolizer(self, executable, symbolizer):
        """Sets the paths to both the wrapper and LLVM symbolizers."""
        if not os.path.exists(executable) or not os.access(executable, os.X_OK):
            raise Host.ConfigError('Invalid symbolize binary: ' + executable)
        if not os.path.exists(symbolizer) or not os.access(symbolizer, os.X_OK):
            raise Host.ConfigError('Invalid LLVM symbolizer: ' + symbolizer)
        self._symbolizer_exec = executable
        self._llvm_symbolizer = symbolizer

    def set_fuzzers_json(self, json_file):
        """Sets the path to the build file with fuzzer metadata."""
        if not os.path.exists(json_file):
            raise Host.ConfigError('Unable to find list of fuzzers.')
        self.fuzzers = []
        with open(json_file) as f:
            fuzz_specs = json.load(f)
        for fuzz_spec in fuzz_specs:
            pkg = fuzz_spec['fuzzers_package']
            for tgt in fuzz_spec['fuzzers']:
                self.fuzzers.append((pkg, tgt))
        self.fuzzers.sort()

    def set_build_dir(self, build_dir):
        """Configure the host using data from a build directory."""
        self.set_zxtools(Host.join(build_dir + '.zircon', 'tools'))
        clang_dir = os.path.join(
            'prebuilt', 'third_party', 'clang', self._platform)
        self.set_symbolizer(
            Host.join(
                self.get_host_out_dir(), 'symbolize'),
            Host.join(clang_dir, 'bin', 'llvm-symbolizer'))
        self.add_build_ids(Host.join('prebuilt_build_ids'))
        self.add_build_ids(Host.join(clang_dir, 'lib', 'debug', '.build-id'))
        self.add_build_ids(Host.join(build_dir, '.build-id'))
        self.add_build_ids(Host.join(build_dir + '.zircon', '.build-id'))
        json_file = Host.join(build_dir, 'fuzzers.json')
        # fuzzers.json isn't emitted in release builds
        if os.path.exists(json_file):
            self.set_fuzzers_json(json_file)
        self.build_dir = build_dir

    def create_process(self, args, **kwargs):
        return Process(args, **kwargs)

    def zircon_tool(self, cmd, logfile=None):
        """Executes a tool found in the ZIRCON_BUILD_DIR."""
        if not self._zxtools:
            raise Host.ConfigError('Zircon host tools unavailable.')
        if not os.path.isabs(cmd[0]):
            cmd[0] = os.path.join(self._zxtools, cmd[0])
        if not os.path.exists(cmd[0]):
            raise Host.ConfigError('Unable to find Zircon host tool: ' + cmd[0])
        p = self.create_process(cmd)
        if logfile:
            p.stdout = logfile
            p.stderr = subprocess.STDOUT
            p.popen()
        else:
            p.stderr = Host.DEVNULL
            return p.check_output().strip()

    def killall(self, process):
        """ Invokes killall on the process name."""
        p = self.create_process(
            ['killall', process], stdout=Host.DEVNULL, stderr=Host.DEVNULL)
        p.call()

    def symbolize(self, raw):
        """Symbolizes backtraces in a log file using the current build.

        Attributes:
            raw: Bytes representing unsymbolized lines.

        Returns:
            Bytes representing symbolized lines.
        """
        if not self._symbolizer_exec:
            raise Host.ConfigError('Symbolizer executable not set.')
        if len(self._ids) == 0:
            raise Host.ConfigError('Build IDs not set.')
        if not self._llvm_symbolizer:
            raise Host.ConfigError('LLVM symbolizer not set.')

        # Symbolize
        args = [
            self._symbolizer_exec, '-llvm-symbolizer', self._llvm_symbolizer
        ]
        for build_id_dir in self._ids:
            args += ['-build-id-dir', build_id_dir]
        p = self.create_process(args)
        p.stdin = subprocess.PIPE
        p.stdout = subprocess.PIPE
        proc = p.popen()
        out, _ = proc.communicate(raw)
        if proc.returncode != 0:
            out = ''
        return re.sub(r'[0-9\[\]\.]*\[klog\] INFO: ', '', out)

    def notify_user(self, title, body):
        """Displays a message to the user in a platform-specific way"""
        args = ['which', 'notify-send']
        p = self.create_process(args, stdout=Host.DEVNULL, stderr=Host.DEVNULL)

        if self._platform == 'mac-x64':
            args = [
                'osascript', '-e',
                'display notification "' + body + '" with title "' + title + '"'
            ]
        elif p.call() == 0:
            args = ['notify-send', title, body]
        else:
            args = ['wall', title + '; ' + body]
        return self.create_process(
            args, stdout=Host.DEVNULL, stderr=Host.DEVNULL).call()

    def snapshot(self):
        integration = Host.join('integration')
        if not os.path.isdir(integration):
            raise Host.ConfigError('Missing integration repo.')
        p = self.create_process(
            ['git', 'rev-parse', 'HEAD'], stderr=Host.DEVNULL, cwd=integration)
        return p.check_output().strip()
