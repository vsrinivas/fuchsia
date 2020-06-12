#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

import test_env
from lib.factory import Factory
from cli_fake import FakeCLI
from lib.buildenv import BuildEnv
from lib.device import Device


class FakeFactory(Factory):
    """Fake factory that creates objects for testing.

       Unlike the real factory, this object caches and reuses created BuildEnvs
       and Devices. It also allows tests to access created objects for
       examination.

       Attributes:
         buildenv:      The associated BuildEnv object.
         device:        The associated Device object.
         fuzzer:        The most recently created FakeFuzzer object.
    """

    def __init__(self):
        super(FakeFactory, self).__init__()
        self._cli = FakeCLI()
        self._parser = None
        self._buildenv = None
        self._device = None
        self._fuzzer = None

    # Factory created objects, lazily instantiated.

    @property
    def parser(self):
        """The associated ArgParser object."""
        if not self._parser:
            self._parser = self.create_parser()
        return self._parser

    @property
    def buildenv(self):
        """The associated BuildEnv object."""
        if not self._buildenv:
            self._buildenv = self.create_buildenv()
        return self._buildenv

    @property
    def device(self):
        """The associated Device object."""
        if not self._device:
            self._device = self.create_device()
        return self._device

    @property
    def fuzzer(self):
        """The most recently created Fuzzer object."""
        assert self._fuzzer, 'No fuzzer created.'
        return self._fuzzer

    # Methods to create objects.

    def create_buildenv(self):
        """Returns the factory's build environment, creating it if needed."""
        fuchsia_dir = self.cli.getenv('FUCHSIA_DIR')
        self.cli.mkdir(fuchsia_dir)
        buildenv = BuildEnv(self.cli, fuchsia_dir)
        build_dir = 'build_dir'
        self.cli.mkdir(buildenv.path(build_dir))
        self.cli.touch(buildenv.path(build_dir, 'host_x64', 'symbolize'))
        self.cli.touch(
            buildenv.path(
                'prebuilt', 'third_party', 'clang', self.cli.platform, 'bin',
                'llvm-symbolizer'))
        self.cli.mkdir(
            buildenv.path(
                'prebuilt', 'third_party', 'clang', self.cli.platform, 'lib',
                'debug', '.build-id'))
        self.cli.mkdir(buildenv.path(build_dir, '.build-id'))
        self.cli.mkdir(buildenv.path(build_dir + '.zircon', '.build-id'))
        self.cli.touch(buildenv.path(build_dir, 'ssh-keys', 'ssh_config'))
        buildenv.configure(build_dir)
        buildenv.add_fuzzer('fake-package1', 'fake-target1')
        buildenv.add_fuzzer('fake-package1', 'fake-target2')
        buildenv.add_fuzzer('fake-package1', 'fake-target3')
        buildenv.add_fuzzer('fake-package2', 'fake-target1')
        buildenv.add_fuzzer('fake-package2', 'fake-target11')
        buildenv.add_fuzzer('fake-package2', 'an-extremely-verbose-target-name')
        return buildenv

    def create_device(self):
        """Returns the factory's device, creating it if needed."""
        device = Device(self.create_buildenv(), '::1')
        device.configure()
        return device

    def create_fuzzer(self, args, device=None):
        self._fuzzer = super(FakeFactory, self).create_fuzzer(
            args, device=device)
        return self._fuzzer
