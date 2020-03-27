#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
import subprocess
import tempfile

import test_env
from lib.args import Args
from lib.device import Device
from lib.host import Host

from device_mock import MockDevice
from host_mock import MockHost


class TestDevice(unittest.TestCase):
    """ Tests lib.Device. See MockDevice for additional details."""

    def test_from_args(self):
        host = MockHost()
        parser = Args.make_parser('description', name_required=False)
        # netaddr should get called with 'just-four-random-words', and fail
        with self.assertRaises(RuntimeError):
            args = parser.parse_args(['--device', 'just-four-random-words'])
            device = Device.from_args(host, args)

    def test_set_ssh_config(self):
        mock = MockDevice()
        with self.assertRaises(Host.ConfigError):
            mock.set_ssh_config('no_such_config')
        if not os.getenv('FUCHSIA_DIR'):
            return
        build_dir = mock.host.find_build_dir()
        ssh_config = Host.join(build_dir, 'ssh-keys', 'ssh_config')
        if not os.path.exists(ssh_config):
            return
        mock.set_ssh_config(ssh_config)
        cmd = ' '.join(mock.get_ssh_cmd(['ssh']))
        self.assertIn('ssh', cmd)
        self.assertIn(' -F ' + ssh_config, cmd)

    def test_set_ssh_identity(self):
        mock = MockDevice()
        with self.assertRaises(Host.ConfigError):
            mock.set_ssh_identity('no_such_identity')
        if not os.getenv('FUCHSIA_DIR'):
            return
        identity_file = Host.join('.ssh', 'pkey')
        if not os.path.exists(identity_file):
            return
        mock.set_ssh_identity(identity_file)
        cmd = ' '.join(mock.get_ssh_cmd(['scp']))
        self.assertIn('scp', cmd)
        self.assertIn(' -i ' + identity_file, cmd)

    def test_set_ssh_option(self):
        mock = MockDevice()
        mock.set_ssh_option('StrictHostKeyChecking no')
        mock.set_ssh_option('UserKnownHostsFile=/dev/null')
        cmd = ' '.join(mock.get_ssh_cmd(['ssh']))
        self.assertIn(' -o StrictHostKeyChecking no', cmd)
        self.assertIn(' -o UserKnownHostsFile=/dev/null', cmd)

    def test_init(self):
        mock = MockDevice(port=51823)
        cmd = ' '.join(mock.get_ssh_cmd(['ssh', '::1', 'some-command']))
        self.assertIn(' -p 51823 ', cmd)

    def test_set_ssh_verbosity(self):
        mock = MockDevice()
        mock.set_ssh_verbosity(3)
        cmd = ' '.join(mock.get_ssh_cmd(['ssh', '::1', 'some-command']))
        self.assertIn(' -vvv ', cmd)
        mock.set_ssh_verbosity(1)
        cmd = ' '.join(mock.get_ssh_cmd(['ssh', '::1', 'some-command']))
        self.assertIn(' -v ', cmd)
        self.assertNotIn(' -vvv ', cmd)
        mock.set_ssh_verbosity(0)
        cmd = ' '.join(mock.get_ssh_cmd(['ssh', '::1', 'some-command']))
        self.assertNotIn(' -v ', cmd)

    def test_ssh(self):
        mock = MockDevice()
        mock.ssh(['some-command', '--with', 'some-argument']).check_call()
        self.assertIn(
            ' '.join(
                mock.get_ssh_cmd(
                    ['ssh', '::1', 'some-command', '--with some-argument'])),
            mock.host.history)

    def test_getpids(self):
        mock = MockDevice()
        pids = mock.getpids()
        self.assertTrue('mock-target1' in pids)
        self.assertEqual(pids['mock-target1'], 7412221)
        self.assertEqual(pids['an-extremely-verbose-target-name'], 7412223)

    def test_ls(self):
        mock = MockDevice()
        files = mock.ls('path-to-some-corpus')
        self.assertIn(
            ' '.join(
                mock.get_ssh_cmd(
                    ['ssh', '::1', 'ls', '-l', 'path-to-some-corpus'])),
            mock.host.history)
        self.assertTrue('feac37187e77ff60222325cf2829e2273e04f2ea' in files)
        self.assertEqual(
            files['feac37187e77ff60222325cf2829e2273e04f2ea'], 1796)

    def test_rm(self):
        mock = MockDevice()
        mock.rm('path-to-some-file')
        mock.rm('path-to-some-directory', recursive=True)
        self.assertIn(
            ' '.join(
                mock.get_ssh_cmd(
                    ['ssh', '::1', 'rm', '-f', 'path-to-some-file'])),
            mock.host.history)
        self.assertIn(
            ' '.join(
                mock.get_ssh_cmd(
                    ['ssh', '::1', 'rm', '-rf', 'path-to-some-directory'])),
            mock.host.history)

    def test_fetch(self):
        mock = MockDevice()
        with self.assertRaises(ValueError):
            mock.fetch('foo', 'not-likely-to-be-a-directory')
        mock.fetch('remote-path', '/tmp')
        self.assertIn(
            ' '.join(mock.get_ssh_cmd(['scp', '[::1]:remote-path', '/tmp'])),
            mock.host.history)
        mock.fetch('corpus/*', '/tmp')
        self.assertIn(
            ' '.join(mock.get_ssh_cmd(['scp', '[::1]:corpus/*', '/tmp'])),
            mock.host.history)
        mock.delay = 2
        with self.assertRaises(subprocess.CalledProcessError):
            mock.fetch('delayed', '/tmp')
        mock.delay = 2
        with self.assertRaises(subprocess.CalledProcessError):
            mock.fetch('delayed', '/tmp', retries=1)
        mock.delay = 2
        mock.fetch('delayed', '/tmp', retries=2)
        self.assertIn(
            ' '.join(mock.get_ssh_cmd(['scp', '[::1]:delayed', '/tmp'])),
            mock.host.history)

    def test_store(self):
        mock = MockDevice()
        mock.store(tempfile.gettempdir(), 'remote-path')
        self.assertIn(
            ' '.join(
                mock.get_ssh_cmd(
                    ['scp', tempfile.gettempdir(), '[::1]:remote-path'])),
            mock.host.history)
        # Ensure globbing works
        mock.host.history = []
        with tempfile.NamedTemporaryFile() as f:
            mock.store('{}/*'.format(tempfile.gettempdir()), 'remote-path')
            for cmd in mock.host.history:
                if cmd.startswith('scp'):
                    self.assertIn(f.name, cmd)
        # No copy if glob comes up empty
        mock.store(os.path.join(tempfile.gettempdir(), '*'), 'remote-path')
        self.assertNotIn(
            ' '.join(
                mock.get_ssh_cmd(
                    ['scp', tempfile.gettempdir(), '[::1]:remote-path'])),
            mock.host.history)


if __name__ == '__main__':
    unittest.main()
