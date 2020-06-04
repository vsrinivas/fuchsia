#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
import subprocess
import tempfile

import test_env
from lib.args import ArgParser
from lib.device import Device
from lib.host import Host

from device_fake import FakeDevice
from host_fake import FakeHost


class TestDevice(unittest.TestCase):
    """ Tests lib.Device. See FakeDevice for additional details."""

    def test_from_args(self):
        host = FakeHost()
        parser = ArgParser('description')
        parser.require_name(False)
        # netaddr should get called with 'just-four-random-words', and fail
        with self.assertRaises(RuntimeError):
            args = parser.parse_args(['--device', 'just-four-random-words'])
            device = Device.from_args(host, args)

    def test_set_ssh_config(self):
        device = FakeDevice()
        with self.assertRaises(Host.ConfigError):
            device.set_ssh_config('no_such_config')
        if not os.getenv('FUCHSIA_DIR'):
            return
        build_dir = device.host.find_build_dir()
        ssh_config = Host.join(build_dir, 'ssh-keys', 'ssh_config')
        if not os.path.exists(ssh_config):
            return
        device.set_ssh_config(ssh_config)
        cmd = ' '.join(device.get_ssh_cmd(['ssh']))
        self.assertIn('ssh', cmd)
        self.assertIn(' -F ' + ssh_config, cmd)

    def test_set_ssh_identity(self):
        device = FakeDevice()
        with self.assertRaises(Host.ConfigError):
            device.set_ssh_identity('no_such_identity')
        if not os.getenv('FUCHSIA_DIR'):
            return
        identity_file = Host.join('.ssh', 'pkey')
        if not os.path.exists(identity_file):
            return
        device.set_ssh_identity(identity_file)
        cmd = ' '.join(device.get_ssh_cmd(['scp']))
        self.assertIn('scp', cmd)
        self.assertIn(' -i ' + identity_file, cmd)

    def test_set_ssh_option(self):
        device = FakeDevice()
        device.set_ssh_option('StrictHostKeyChecking no')
        device.set_ssh_option('UserKnownHostsFile=/dev/null')
        cmd = ' '.join(device.get_ssh_cmd(['ssh']))
        self.assertIn(' -o StrictHostKeyChecking no', cmd)
        self.assertIn(' -o UserKnownHostsFile=/dev/null', cmd)

    def test_init(self):
        device = FakeDevice(port=51823)
        cmd = ' '.join(device.get_ssh_cmd(['ssh', '::1', 'some-command']))
        self.assertIn(' -p 51823 ', cmd)

    def test_set_ssh_verbosity(self):
        device = FakeDevice()
        device.set_ssh_verbosity(3)
        cmd = ' '.join(device.get_ssh_cmd(['ssh', '::1', 'some-command']))
        self.assertIn(' -vvv ', cmd)
        device.set_ssh_verbosity(1)
        cmd = ' '.join(device.get_ssh_cmd(['ssh', '::1', 'some-command']))
        self.assertIn(' -v ', cmd)
        self.assertNotIn(' -vvv ', cmd)
        device.set_ssh_verbosity(0)
        cmd = ' '.join(device.get_ssh_cmd(['ssh', '::1', 'some-command']))
        self.assertNotIn(' -v ', cmd)

    def test_ssh(self):
        device = FakeDevice()
        device.ssh(['some-command', '--with', 'some-argument']).check_call()
        self.assertIn(
            ' '.join(
                device.get_ssh_cmd(
                    ['ssh', '::1', 'some-command', '--with some-argument'])),
            device.host.history)

    def test_getpids(self):
        device = FakeDevice()
        pids = device.getpids()
        self.assertTrue('fake-package1/fake-target1' in pids)
        self.assertEqual(pids['fake-package1/fake-target1'], 7412221)
        self.assertEqual(
            pids['fake-package2/an-extremely-verbose-target-name'], 7412223)

    def test_ls(self):
        device = FakeDevice()
        files = device.ls('path-to-some-corpus')
        self.assertIn(
            ' '.join(
                device.get_ssh_cmd(
                    ['ssh', '::1', 'ls', '-l', 'path-to-some-corpus'])),
            device.host.history)
        self.assertTrue('feac37187e77ff60222325cf2829e2273e04f2ea' in files)
        self.assertEqual(
            files['feac37187e77ff60222325cf2829e2273e04f2ea'], 1796)

    def test_rm(self):
        device = FakeDevice()
        device.rm('path-to-some-file')
        device.rm('path-to-some-directory', recursive=True)
        self.assertIn(
            ' '.join(
                device.get_ssh_cmd(
                    ['ssh', '::1', 'rm', '-f', 'path-to-some-file'])),
            device.host.history)
        self.assertIn(
            ' '.join(
                device.get_ssh_cmd(
                    ['ssh', '::1', 'rm', '-rf', 'path-to-some-directory'])),
            device.host.history)

    def test_fetch(self):
        device = FakeDevice()
        with self.assertRaises(ValueError):
            device.fetch('foo', 'not-likely-to-be-a-directory')
        device.fetch('remote-path', '/tmp')
        self.assertIn(
            ' '.join(device.get_ssh_cmd(['scp', '[::1]:remote-path', '/tmp'])),
            device.host.history)
        device.fetch('corpus/*', '/tmp')
        self.assertIn(
            ' '.join(device.get_ssh_cmd(['scp', '[::1]:corpus/*', '/tmp'])),
            device.host.history)
        device.delay = 2
        with self.assertRaises(subprocess.CalledProcessError):
            device.fetch('delayed', '/tmp')
        device.delay = 2
        with self.assertRaises(subprocess.CalledProcessError):
            device.fetch('delayed', '/tmp', retries=1)
        device.delay = 2
        device.fetch('delayed', '/tmp', retries=2)
        self.assertIn(
            ' '.join(device.get_ssh_cmd(['scp', '[::1]:delayed', '/tmp'])),
            device.host.history)

    def test_store(self):
        device = FakeDevice()
        device.store(tempfile.gettempdir(), 'remote-path')
        self.assertIn(
            ' '.join(
                device.get_ssh_cmd(
                    ['scp', tempfile.gettempdir(), '[::1]:remote-path'])),
            device.host.history)
        # Ensure globbing works
        device.host.history = []
        with tempfile.NamedTemporaryFile() as f:
            device.store('{}/*'.format(tempfile.gettempdir()), 'remote-path')
            for cmd in device.host.history:
                if cmd.startswith('scp'):
                    self.assertIn(f.name, cmd)
        # No copy if glob comes up empty
        device.store(os.path.join(tempfile.gettempdir(), '*'), 'remote-path')
        self.assertNotIn(
            ' '.join(
                device.get_ssh_cmd(
                    ['scp', tempfile.gettempdir(), '[::1]:remote-path'])),
            device.host.history)


if __name__ == '__main__':
    unittest.main()
