#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
import subprocess

import test_env
from lib.device import Device
from test_case import TestCase


class DeviceTest(TestCase):

    # These tests don't use a factory-created Device.
    # This to avoid having the factory add SSH options.

    def test_ssh_config(self):
        device = Device(self.host, '::1')
        with self.assertRaises(ValueError):
            device.ssh_config = 'no_such_config'
        ssh_config = 'ssh_config'
        self.cli.touch(ssh_config)
        device.ssh_config = ssh_config
        self.assertEqual(['-F', ssh_config], device.ssh_opts())

    def test_ssh_identity(self):
        device = Device(self.host, '::1')
        with self.assertRaises(ValueError):
            device.ssh_identity = 'no_such_identity'
        ssh_identity = 'ssh_identity'
        self.cli.touch(ssh_identity)
        device.ssh_identity = ssh_identity
        self.assertEqual(['-i', ssh_identity], device.ssh_opts())

    def test_ssh_option(self):
        device = Device(self.host, '::1')
        device.ssh_options = ['StrictHostKeyChecking no']
        device.ssh_options.append('UserKnownHostsFile=/dev/null')
        self.assertEqual(
            [
                '-o',
                'StrictHostKeyChecking no',
                '-o',
                'UserKnownHostsFile=/dev/null',
            ], device.ssh_opts())

    def test_ssh_verbosity(self):
        device = Device(self.host, '::1')
        self.assertEqual(device.ssh_verbosity, 0)

        with self.assertRaises(ValueError):
            device.ssh_verbosity = 4

        device.ssh_verbosity = 3
        self.assertEqual(['-vvv'], device.ssh_opts())

        device.ssh_verbosity = 2
        self.assertEqual(['-vv'], device.ssh_opts())

        device.ssh_verbosity = 1
        self.assertEqual(['-v'], device.ssh_opts())

        device.ssh_verbosity = 0
        self.assertEqual([], device.ssh_opts())

        with self.assertRaises(ValueError):
            device.ssh_verbosity = -1

    def test_configure(self):
        device = Device(self.host, '::1')
        device.configure()
        self.assertEqual(
            device.ssh_config,
            self.host.fxpath(self.host.build_dir, 'ssh-keys', 'ssh_config'))
        self.assertFalse(device.ssh_identity)
        self.assertFalse(device.ssh_options)
        self.assertFalse(device.ssh_verbosity)

    # These tests use a Device created by the TestCase

    def test_ssh(self):
        cmd = ['some-command', '--with', 'some-argument']
        self.device.ssh(cmd).check_call()
        self.assertSsh(*cmd)

    def test_getpid(self):
        cmd = ['cs']
        self.set_outputs(
            cmd, [
                '  http.cmx[20963]: fuchsia-pkg://fuchsia.com/http#meta/http.cmx',
                '  fake-target1.cmx[7412221]: fuchsia-pkg://fuchsia.com/fake-package1#meta/fake-target1.cmx',
                '  fake-target2.cmx[7412222]: fuchsia-pkg://fuchsia.com/fake-package1#meta/fake-target2.cmx',
                '  an-extremely-verbose-target-name.cmx[7412223]: fuchsia-pkg://fuchsia.com/fake-package2#meta/an-extremely-verbose-target-name.cmx',
            ],
            ssh=True)

        # First request invokes "cs".
        self.assertEqual(
            self.device.getpid(
                'fake-package2', 'an-extremely-verbose-target-name'), 7412223)
        self.assertSsh(*cmd)

        # PIDs are retrieved for all packaged executables.
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target2'), 7412222)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target1'), 7412221)
        self.assertEqual(self.device.getpid('http', 'http'), 20963)

        # PIDs are cached until refresh.
        self.set_outputs(cmd, [], ssh=True)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target1'), 7412221)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target1', refresh=True),
            -1)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target2'), -1)

    def test_ls(self):
        corpus_dir = 'path-to-some-corpus'
        cmd = ['ls', '-l', corpus_dir]
        self.set_outputs(
            cmd, [
                '-rw-r--r-- 1 0 0 1796 Mar 19 17:25 feac37187e77ff60222325cf2829e2273e04f2ea',
                '-rw-r--r-- 1 0 0  124 Mar 18 22:02 ff415bddb30e9904bccbbd21fb5d4aa9bae9e5a5',
            ],
            ssh=True)
        files = self.device.ls(corpus_dir)
        self.assertSsh(*cmd)
        self.assertEqual(
            files['feac37187e77ff60222325cf2829e2273e04f2ea'], 1796)

    def test_rm(self):
        some_file = 'path-to-some-file'
        some_dir = 'path-to-some-directory'
        self.device.remove(some_file)
        self.device.remove(some_dir, recursive=True)
        self.assertSsh('rm -f path-to-some-file')
        self.assertSsh('rm -rf path-to-some-directory')

    def test_fetch(self):
        local_path = 'test_fetch'
        remote_path = 'remote-path'

        # Fails due to missing pathname.
        with self.assertRaises(ValueError):
            self.device.fetch(remote_path, local_path)

        self.cli.mkdir(local_path)
        self.device.fetch(remote_path, local_path)
        self.assertScpFrom(remote_path, local_path)

        corpus_dir = os.path.join(remote_path, '*')
        self.device.fetch(corpus_dir, local_path)
        self.assertScpFrom(corpus_dir, local_path)

    def test_store(self):
        local_path = 'test_store'
        remote_path = 'remote-path'

        foo = os.path.join(local_path, 'foo')
        self.cli.touch(foo)
        self.device.store(foo, remote_path)
        self.assertScpTo(foo, remote_path)

        bar = os.path.join(local_path, 'bar')
        baz = os.path.join(local_path, 'baz')
        self.cli.touch(bar)
        self.cli.touch(baz)
        self.assertEqual(
            self.device.store(os.path.join(local_path, '*'), remote_path),
            [bar, baz, foo])
        self.assertScpTo(bar, baz, foo, remote_path)


if __name__ == '__main__':
    unittest.main()
