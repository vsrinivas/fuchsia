#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
import subprocess
import tempfile

import test_env
from lib.device import Device
from lib.host import Host

from host_test import HostTestCase

from device_fake import FakeDevice
from host_fake import FakeHost


class DeviceTestCase(HostTestCase):
    """Test case with additional, device-specific assertions."""

    def assertRan(self, host, *args):
        self.assertIn(' '.join(args), host.history)

    def assertScp(self, device, *args):
        """Asserts a previous call was made to device.ssh with args."""
        self.assertRan(device.host, *device._scp_cmd(list(args)))

    def assertSsh(self, device, *args):
        """Asserts a previous call was made to device.scp with cmd."""
        self.assertRan(device.host, *device._ssh_cmd(list(args)))


class TestDevice(DeviceTestCase):
    """ Tests lib.Device. See FakeDevice for additional details."""

    def test_configure(self):
        device = FakeDevice(autoconfigure=False)
        self.assertEqual(device.ssh_config, None)

        # Fails due to missing paths
        with self.assertRaises(ValueError):
            device.configure()

        device.add_fake_pathnames()
        device.configure()
        ssh_config = device.host.fxpath(
            device.host.build_dir, 'ssh-keys', 'ssh_config')
        self.assertEqual(device.ssh_config, ssh_config)
        self.assertEqual(['-F', ssh_config], device._ssh_opts())

    def test_ssh_identity(self):
        device = FakeDevice(autoconfigure=False)
        self.assertEqual(device.ssh_identity, None)

        with self.assertRaises(ValueError):
            device.ssh_identity = 'no_such_identity'

        ssh_identity = device.host.fxpath('.ssh', 'pkey')
        device.host.pathnames.append(ssh_identity)
        device.ssh_identity = ssh_identity
        self.assertEqual(['-i', ssh_identity], device._ssh_opts())

    def test_ssh_option(self):
        device = FakeDevice(autoconfigure=False)
        self.assertEqual(device.ssh_options, [])
        device.ssh_options = ['StrictHostKeyChecking no']
        device.ssh_options.append('UserKnownHostsFile=/dev/null')
        self.assertEqual(
            [
                '-o',
                'StrictHostKeyChecking no',
                '-o',
                'UserKnownHostsFile=/dev/null',
            ], device._ssh_opts())

    def test_ssh_verbosity(self):
        device = FakeDevice(autoconfigure=False)
        self.assertEqual(device.ssh_verbosity, 0)

        with self.assertRaises(ValueError):
            device.ssh_verbosity = 4

        device.ssh_verbosity = 3
        self.assertEqual(['-vvv'], device._ssh_opts())

        device.ssh_verbosity = 2
        self.assertEqual(['-vv'], device._ssh_opts())

        device.ssh_verbosity = 1
        self.assertEqual(['-v'], device._ssh_opts())

        device.ssh_verbosity = 0
        self.assertEqual([], device._ssh_opts())

        with self.assertRaises(ValueError):
            device.ssh_verbosity = -1

    def test_ssh(self):
        device = FakeDevice()
        cmd = ['some-command', '--with', 'some-argument']
        device.ssh(cmd).check_call()
        self.assertSsh(device, *cmd)

    def test_getpid(self):
        device = FakeDevice()
        cmd = ['cs']
        device.add_ssh_response(
            cmd, [
                '  http.cmx[20963]: fuchsia-pkg://fuchsia.com/http#meta/http.cmx',
                '  fake-target1.cmx[7412221]: fuchsia-pkg://fuchsia.com/fake-package1#meta/fake-target1.cmx',
                '  fake-target2.cmx[7412222]: fuchsia-pkg://fuchsia.com/fake-package1#meta/fake-target2.cmx',
                '  an-extremely-verbose-target-name.cmx[7412223]: fuchsia-pkg://fuchsia.com/fake-package2#meta/an-extremely-verbose-target-name.cmx',
            ])

        # First request invokes "cs".
        self.assertEqual(
            device.getpid('fake-package2', 'an-extremely-verbose-target-name'),
            7412223)
        self.assertSsh(device, *cmd)

        # PIDs are retrieved for all packaged executables.
        self.assertEqual(
            device.getpid('fake-package1', 'fake-target2'), 7412222)
        self.assertEqual(
            device.getpid('fake-package1', 'fake-target1'), 7412221)
        self.assertEqual(device.getpid('http', 'http'), 20963)

        # PIDs are cached until refresh.
        device.clear_ssh_response(cmd)
        self.assertEqual(
            device.getpid('fake-package1', 'fake-target1'), 7412221)
        self.assertEqual(
            device.getpid('fake-package1', 'fake-target1', refresh=True), -1)
        self.assertEqual(device.getpid('fake-package1', 'fake-target2'), -1)

    def test_ls(self):
        device = FakeDevice()
        corpus_dir = 'path-to-some-corpus'
        cmd = ['ls', '-l', corpus_dir]
        device.add_ssh_response(
            cmd, [
                '-rw-r--r-- 1 0 0 1796 Mar 19 17:25 feac37187e77ff60222325cf2829e2273e04f2ea',
                '-rw-r--r-- 1 0 0  124 Mar 18 22:02 ff415bddb30e9904bccbbd21fb5d4aa9bae9e5a5',
            ])
        files = device.ls(corpus_dir)
        self.assertSsh(device, *cmd)
        self.assertEqual(
            files['feac37187e77ff60222325cf2829e2273e04f2ea'], 1796)

    def test_rm(self):
        device = FakeDevice()
        some_file = 'path-to-some-file'
        some_dir = 'path-to-some-directory'
        device.rm(some_file)
        device.rm(some_dir, recursive=True)
        self.assertSsh(device, 'rm -f path-to-some-file')
        self.assertSsh(device, 'rm -rf path-to-some-directory')

    def test_fetch(self):
        device = FakeDevice()
        local_path = 'test_fetch'
        remote_path = 'remote-path'

        # Fails due to missing pathname.
        with self.assertRaises(ValueError):
            device.fetch(remote_path, local_path)

        device.host.mkdir(local_path)
        device.fetch(remote_path, local_path)
        self.assertScp(device, device._rpath(remote_path), local_path)

        corpus_dir = os.path.join(remote_path, '*')
        device.fetch(corpus_dir, local_path)
        self.assertScp(device, device._rpath(corpus_dir), local_path)

        cmd = [device._rpath(remote_path), local_path]
        cmd_str = ' '.join(device._scp_cmd(cmd))
        device.host.failures[cmd_str] = 10
        with self.assertRaises(subprocess.CalledProcessError):
            device.fetch(remote_path, local_path)

        device.host.failures[cmd_str] = 10
        with self.assertRaises(subprocess.CalledProcessError):
            device.fetch(remote_path, local_path, retries=9, delay_ms=1)

        device.host.failures[cmd_str] = 10
        device.fetch(remote_path, local_path, retries=10, delay_ms=1)
        self.assertScp(device, *cmd)

    def test_store(self):
        device = FakeDevice()
        local_path = 'test_store'
        remote_path = 'remote-path'

        foo = os.path.join(local_path, 'foo')
        device.host.pathnames += [foo]
        device.store(foo, remote_path)
        self.assertScp(device, foo, device._rpath(remote_path))

        bar = os.path.join(local_path, 'bar')
        baz = os.path.join(local_path, 'baz')
        device.host.pathnames += [bar, baz]
        self.assertEqual(
            device.store(os.path.join(local_path, '*'), remote_path),
            [foo, bar, baz])
        self.assertScp(device, foo, bar, baz, device._rpath(remote_path))


if __name__ == '__main__':
    unittest.main()
