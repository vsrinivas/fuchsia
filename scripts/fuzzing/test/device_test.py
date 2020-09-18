#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
import subprocess

import test_env
from lib.device import Device
from test_case import TestCaseWithFactory


class DeviceTest(TestCaseWithFactory):

    # These tests don't use a factory-created Device.
    # This to avoid having the factory add SSH options.

    def test_ssh_config(self):
        device = Device(self.factory, '::1')
        self.assertFalse(device.ssh_config)

        with self.assertRaises(ValueError):
            device.ssh_config = 'no_such_config'
        ssh_config = 'ssh_config'
        self.host.touch(ssh_config)
        device.ssh_config = ssh_config
        self.assertEqual(['-F', ssh_config], device.ssh_opts())

    def test_ssh_identity(self):
        device = Device(self.factory, '::1')
        self.assertFalse(device.ssh_identity)

        with self.assertRaises(ValueError):
            device.ssh_identity = 'no_such_identity'
        ssh_identity = 'ssh_identity'
        self.host.touch(ssh_identity)
        device.ssh_identity = ssh_identity
        self.assertEqual(['-i', ssh_identity], device.ssh_opts())

    def test_ssh_option(self):
        device = Device(self.factory, '::1')
        self.assertEqual(device.ssh_options, [])

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
        device = Device(self.factory, '::1')
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

    def test_reachable(self):
        cmd = ['true']
        self.assertTrue(self.device.reachable)
        self.assertSsh(*cmd)

    def test_unreachable(self):
        cmd = ['true']
        process = self.get_process(cmd, ssh=True)
        process.succeeds = False
        self.assertFalse(self.device.reachable)
        self.assertSsh(*cmd)

    def test_configure(self):
        device = Device(self.factory, '::1')
        device.configure()
        self.assertEqual(
            device.ssh_config,
            self.buildenv.path(
                self.buildenv.build_dir, 'ssh-keys', 'ssh_config'))
        self.assertFalse(device.ssh_identity)
        self.assertFalse(device.ssh_options)
        self.assertFalse(device.ssh_verbosity)

    # These tests use a Device created by the TestCase

    def test_ssh(self):
        cmd = ['some-command', '--with', 'some-argument']
        self.device.ssh(cmd).check_call()
        self.assertSsh(*cmd)

    def test_has_cs_info(self):
        url1 = 'fuchsia-pkg://fuchsia.com/http#meta/http.cmx'
        url2 = 'fuchsia-pkg://fuchsia.com/fake-package1#meta/fake-target1.cmx'
        url3 = 'fuchsia-pkg://fuchsia.com/fake-package1#meta/fake-target2.cmx'
        url4 = 'fuchsia-pkg://fuchsia.com/an-extremely-verbose-target-package#meta/an-extremely-verbose-target-executable.cmx'

        self.set_running(url1)
        self.set_running(url2, duration=10)
        self.set_running(url3, duration=10)
        self.set_running(url4)

        # Can check various URLs
        self.assertTrue(self.device.has_cs_info(url1))
        self.assertTrue(self.device.has_cs_info(url2))
        self.assertTrue(self.device.has_cs_info(url3))
        self.assertTrue(self.device.has_cs_info(url4))

        # URLs are cached until refresh.
        self.host.sleep(10)
        self.assertTrue(self.device.has_cs_info(url1))
        self.assertTrue(self.device.has_cs_info(url2))
        self.assertTrue(self.device.has_cs_info(url3))
        self.assertTrue(self.device.has_cs_info(url4))

        self.assertTrue(self.device.has_cs_info(url1, refresh=True))
        self.assertFalse(self.device.has_cs_info(url2))
        self.assertFalse(self.device.has_cs_info(url3))
        self.assertTrue(self.device.has_cs_info(url4))

    def test_isfile(self):
        some_file = 'path-to-some-file'
        cmd = ['test', '-f', some_file]
        process = self.get_process(cmd, ssh=True)
        process.succeeds = False
        self.assertFalse(self.device.isfile(some_file))
        self.assertSsh(*cmd)
        process.succeeds = True
        self.assertTrue(self.device.isfile(some_file))
        self.assertSsh(*cmd)

    def test_isdir(self):
        some_dir = 'path-to-some-directory'
        cmd = ['test', '-d', some_dir]
        process = self.get_process(cmd, ssh=True)
        process.succeeds = False
        self.assertFalse(self.device.isdir(some_dir))
        self.assertSsh(*cmd)
        process.succeeds = True
        self.assertTrue(self.device.isdir(some_dir))
        self.assertSsh(*cmd)

    def test_ls(self):
        corpus_dir = 'path-to-some-corpus'
        cmd = ['ls', '-l', corpus_dir]

        self.touch_on_device(
            '{}/feac37187e77ff60222325cf2829e2273e04f2ea'.format(corpus_dir),
            size=1796)
        self.touch_on_device(
            '{}/ff415bddb30e9904bccbbd21fb5d4aa9bae9e5a5'.format(corpus_dir),
            size=124)
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

        # Fails due to missing local pathname.
        self.assertError(
            lambda: self.device.fetch(local_path, remote_path),
            'No such directory: test_fetch')

        self.host.mkdir(local_path)

        # Fails due to empty source file list.
        with self.assertRaises(ValueError):
            self.device.fetch(local_path)

        self.device.fetch(local_path, remote_path)
        self.assertScpFrom(remote_path, local_path)

        corpus_dir = remote_path + '/*'
        self.device.fetch(local_path, corpus_dir)
        self.assertScpFrom(corpus_dir, local_path)

    def test_store(self):
        local_path = 'test_store'
        remote_path = 'remote-path'

        # Globs must resolve
        self.assertError(
            lambda: self.device.store(
                remote_path, os.path.join(local_path, '*')),
            'No matching files: "test_store/*".')

        # Local path must exist
        foo = os.path.join(local_path, 'foo')
        self.assertError(
            lambda: self.device.store(remote_path, foo),
            'No matching files: "test_store/foo".')

        # Valid
        self.host.touch(foo)
        self.device.store(remote_path, foo)
        self.assertScpTo(foo, remote_path)

        # Valid globs
        bar = os.path.join(local_path, 'bar')
        baz = os.path.join(local_path, 'baz')
        self.host.touch(bar)
        self.host.touch(baz)
        self.device.store(remote_path, os.path.join(local_path, '*'))
        self.assertScpTo(bar, baz, foo, remote_path)


if __name__ == '__main__':
    unittest.main()
