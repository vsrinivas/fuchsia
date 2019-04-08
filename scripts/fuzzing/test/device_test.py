#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import subprocess

import test_env
from lib.args import Args
from lib.device import Device
from lib.host import Host

from device_mock import MockDevice


class TestDevice(unittest.TestCase):
  """ Tests lib.Device. See MockDevice for additional details."""

  def test_from_args(self):
    host = Host()
    parser = Args.make_parser('description', name_required=False)
    # netaddr should get called with 'just-four-random-words', and fail
    with self.assertRaises(RuntimeError):
      args = parser.parse_args(['--device', 'just-four-random-words'])
      device = Device.from_args(host, args)

  def test_ssh(self):
    mock = MockDevice()
    mock.ssh(['some-command', '--with', 'some-argument'])
    self.assertEqual(
        mock.last, 'ssh -F ' + mock.host.ssh_config +
        ' ::1 some-command --with some-argument')

  def test_getpids(self):
    mock = MockDevice()
    pids = mock.getpids()
    self.assertTrue('mock-target1' in pids)
    self.assertEqual(pids['mock-target1'], 7412221)
    self.assertEqual(pids['an-extremely-verbose-target-name'], 7412223)

  def test_ls(self):
    mock = MockDevice()
    files = mock.ls('path-to-some-corpus')
    self.assertEqual(
        mock.last,
        'ssh -F ' + mock.host.ssh_config + ' ::1 ls -l path-to-some-corpus')
    self.assertTrue('feac37187e77ff60222325cf2829e2273e04f2ea' in files)
    self.assertEqual(files['feac37187e77ff60222325cf2829e2273e04f2ea'], 1796)

  def test_fetch(self):
    mock = MockDevice()
    with self.assertRaises(ValueError):
      mock.fetch('foo', 'not-likely-to-be-a-directory')
    mock.fetch('remote-path', '/tmp')
    self.assertEqual(
        mock.last, 'scp -F ' + mock.host.ssh_config + ' [::1]:remote-path /tmp')
    mock.fetch('corpus/*', '/tmp')
    self.assertEqual(mock.last,
                     'scp -F ' + mock.host.ssh_config + ' [::1]:corpus/* /tmp')

  def test_store(self):
    mock = MockDevice()
    mock.store('local-path', 'remote-path')
    self.assertEqual(
        mock.last,
        'scp -F ' + mock.host.ssh_config + ' local-path [::1]:remote-path')


if __name__ == '__main__':
  unittest.main()
