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
        http_pid = self.set_running('http', 'http')
        target1_pid = self.set_running(
            'fake-package1', 'fake-target1', duration=10)
        target2_pid = self.set_running(
            'fake-package1', 'fake-target2', duration=10)
        long_pid = self.set_running(
            'fake-package2', 'an-extremely-verbose-target-name')

        # PIDs are retrieved for all packaged executables.
        self.assertEqual(self.device.getpid('http', 'http'), http_pid)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target1'), target1_pid)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target2'), target2_pid)
        self.assertEqual(
            self.device.getpid(
                'fake-package2', 'an-extremely-verbose-target-name'), long_pid)

        # PIDs are cached until refresh.
        self.cli.sleep(10)
        self.assertEqual(self.device.getpid('http', 'http'), http_pid)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target1'), target1_pid)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target2'), target2_pid)
        self.assertEqual(
            self.device.getpid(
                'fake-package2', 'an-extremely-verbose-target-name'), long_pid)

        self.assertEqual(
            self.device.getpid('http', 'http', refresh=True), http_pid)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target1'), -1)
        self.assertEqual(
            self.device.getpid('fake-package1', 'fake-target2'), -1)
        self.assertEqual(
            self.device.getpid(
                'fake-package2', 'an-extremely-verbose-target-name'), long_pid)

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

    def test_mkdir(self):
        corpus_dir = 'path-to-some-corpus'
        cmd = ['mkdir', '-p', corpus_dir]
        self.device.mkdir(corpus_dir)
        self.assertSsh(*cmd)

    def test_remove(self):
        some_file = 'path-to-some-file'
        cmd = ['rm', '-f', some_file]
        self.device.remove(some_file)
        self.assertSsh(*cmd)

        some_dir = 'path-to-some-directory'
        cmd = ['rm', '-rf', some_dir]
        self.device.remove(some_dir, recursive=True)
        self.assertSsh(*cmd)

    def test_dump_log(self):
        cmd = [
            'log_listener', '--dump_logs', 'yes', '--pretty', 'no', '--some',
            'other-arg'
        ]
        self.device.dump_log('--some', 'other-arg')
        self.assertSsh(*cmd)

    def test_guess_pid(self):
        cmd = [
            'log_listener', '--dump_logs', 'yes', '--pretty', 'no', '--only',
            'reset,Fuzzer,Sanitizer'
        ]
        self.assertEqual(self.device.guess_pid(), -1)
        self.assertSsh(*cmd)

        # Log lines are like '[timestamp][pid][tid][name] data'
        self.set_outputs(
            cmd, [
                '[ignored][31415][ignored][ignored] ignored',
                '[ignored][92653][ignored][ignored] ignored',
                '[malformed][58979]',
            ],
            ssh=True)

        # Should find last well-formed line
        self.assertEqual(self.device.guess_pid(), 92653)

    def test_fetch(self):
        local_path = 'test_fetch'
        remote_path = 'remote-path'

        # Fails due to missing pathname.
        with self.assertRaises(ValueError):
            self.device.fetch(local_path, remote_path)

        self.cli.mkdir(local_path)
        self.device.fetch(local_path, remote_path)
        self.assertScpFrom(remote_path, local_path)

        corpus_dir = remote_path + '/*'
        self.device.fetch(local_path, corpus_dir)
        self.assertScpFrom(corpus_dir, local_path)

    def test_store(self):
        local_path = 'test_store'
        remote_path = 'remote-path'

        foo = os.path.join(local_path, 'foo')
        self.cli.touch(foo)
        self.device.store(remote_path, foo)
        self.assertScpTo(foo, remote_path)

        bar = os.path.join(local_path, 'bar')
        baz = os.path.join(local_path, 'baz')
        self.cli.touch(bar)
        self.cli.touch(baz)
        self.assertEqual(
            self.device.store(remote_path, os.path.join(local_path, '*')),
            [bar, baz, foo])
        self.assertScpTo(bar, baz, foo, remote_path)


if __name__ == '__main__':
    unittest.main()
