#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import test_env
from lib.factory import Factory
from cli_fake import FakeCLI
from lib.host import Host
from lib.device import Device


class FakeFactory(Factory):
    """Fake factory that creates objects for testing.

       Unlike the real factory, this object caches and reuses created hosts and
       devices. It also allows tests to access created objects for examination.
    """

    def __init__(self):
        super(FakeFactory, self).__init__()
        self._cli = FakeCLI()
        self._host = None
        self._device = None
        self._fuzzer = None

    def create_host(self):
        """Returns the factory's fake host, creating it if needed."""
        if not self._host:
            fuchsia_dir = self.cli.getenv('FUCHSIA_DIR')
            self.cli.mkdir(fuchsia_dir)
            host = Host(self.cli, fuchsia_dir)
            self.cli.mkdir(host.fxpath('build_dir'))
            self.cli.touch(host.fxpath('build_dir', 'host_x64', 'symbolize'))
            self.cli.touch(
                host.fxpath(
                    'prebuilt', 'third_party', 'clang', self.cli.platform,
                    'bin', 'llvm-symbolizer'))
            self.cli.mkdir(
                host.fxpath(
                    'prebuilt', 'third_party', 'clang', self.cli.platform,
                    'lib', 'debug', '.build-id'))
            self.cli.mkdir(host.fxpath('build_dir', '.build-id'))
            self.cli.mkdir(host.fxpath('build_dir' + '.zircon', '.build-id'))
            self.cli.touch(host.fxpath('build_dir', 'ssh-keys', 'ssh_config'))
            host.configure('build_dir')
            host._fuzzers = [
                (u'fake-package1', u'fake-target1'),
                (u'fake-package1', u'fake-target2'),
                (u'fake-package1', u'fake-target3'),
                (u'fake-package2', u'fake-target1'),
                (u'fake-package2', u'fake-target11'),
                (u'fake-package2', u'an-extremely-verbose-target-name')
            ]
            self._host = host
        return self._host

    def create_device(self):
        """Returns the factory's fake device, creating it if needed."""
        if not self._device:
            self._device = Device(self.create_host(), '::1')
            self._device.configure()
        return self._device
