#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess

import test_env
from lib.device import Device
from lib.process import Process

from cli_fake import FakeCLI
from host_fake import FakeHost


class FakeDevice(Device):
    """Fake device that uses a fake host and fake PIDs."""

    def __init__(self, host=None, autoconfigure=True):
        if not host:
            host = FakeHost()
        super(FakeDevice, self).__init__(host, '::1')
        # Arbitrary starting PID number
        self._pid = 10000
        if autoconfigure:
            self.add_fake_pathnames()
            self.configure()

    def add_fake_pathnames(self):
        """Adds the fake paths for this object to the fake host."""
        host = self.host
        host.pathnames.append(
            host.fxpath(host.build_dir, 'ssh-keys', 'ssh_config'))

    def add_ssh_response(self, args, response):
        """Sets the output from running an SSH command."""
        cmd_str = ' '.join(self._ssh_cmd(args))
        if cmd_str in self.host.responses:
            self.host.responses[cmd_str] += response
        else:
            self.host.responses[cmd_str] = response

    def clear_ssh_response(self, args):
        """Clears the output for running an SSH command."""
        cmd_str = ' '.join(self._ssh_cmd(args))
        if cmd_str in self.host.responses:
            del self.host.responses[cmd_str]

    def add_fake_pid(self, package, executable, refresh=True):
        """Marks a packaged executable as running on device."""
        self._pid += 1
        self.add_ssh_response(
            ['cs'], [
                '  {}.cmx[{}]: fuchsia-pkg://fuchsia.com/{}#meta/{}.cmx'.format(
                    executable, self._pid, package, executable)
            ])
        if refresh:
            self._pids = None
        return self._pid

    def clear_fake_pids(self, refresh=True):
        """Marks all executables as stopped on device."""
        self.clear_ssh_response(['cs'])
        if refresh:
            self._pids = None
