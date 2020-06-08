#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess

import test_env
from lib.device import Device
from lib.process import Process

from host_fake import FakeHost


class FakeDevice(Device):

    def __init__(self, autoconfigure=True):
        self.pid = 10000
        super(FakeDevice, self).__init__(FakeHost(), '::1')
        if autoconfigure:
            self.add_fake_pathnames()
            self.configure()

    def add_fake_pathnames(self):
        host = self.host
        host.pathnames.append(
            host.fxpath(host.build_dir, 'ssh-keys', 'ssh_config'))

    def add_ssh_response(self, args, response):
        cmd_str = ' '.join(self._ssh_cmd(args))
        if cmd_str in self.host.responses:
            self.host.responses[cmd_str] += response
        else:
            self.host.responses[cmd_str] = response

    def clear_ssh_response(self, args):
        cmd_str = ' '.join(self._ssh_cmd(args))
        del self.host.responses[cmd_str]

    def add_fake_pid(self, package, executable):
        self.pid += 1
        cmd = self._cs_cmd()
        response = [
            '  {}.cmx[{}]: fuchsia-pkg://fuchsia.com/{}#meta/{}.cmx'.format(
                executable, self.pid, package, executable)
        ]
        self.add_ssh_response(cmd, response)
        return self.pid

    def clear_fake_pids(self):
        self.clear_ssh_response(self._cs_cmd())
