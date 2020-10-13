#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import sys

import test_env
from lib.factory import Factory
from host_fake import FakeHost
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
        super(FakeFactory, self).__init__(host=FakeHost())

    # Factory created objects, lazily instantiated.

    @property
    def buildenv(self):
        """The associated BuildEnv object."""
        if self._buildenv:
            return self._buildenv
        fuchsia_dir = '/fuchsia_dir'
        self.host.mkdir(fuchsia_dir)
        self.host.setenv('FUCHSIA_DIR', fuchsia_dir)
        buildenv = BuildEnv(self)
        build_dir = buildenv.abspath(fuchsia_dir, "build_dir")
        self.host.mkdir(build_dir)
        self.host.cwd = build_dir
        self.host.touch(build_dir + '/host_x64/symbolize')
        self.host.touch(
            '{}/prebuilt/third_party/clang/{}/bin/llvm-symbolizer'.format(
                fuchsia_dir, self.host.platform))
        self.host.mkdir(
            '{}/prebuilt/third_party/clang/{}/lib/debug/.build-id'.format(
                fuchsia_dir, self.host.platform))
        self.host.touch(
            '{}/prebuilt/third_party/clang/{}/bin/llvm-cov'.format(
                fuchsia_dir, self.host.platform))
        self.host.touch(
            '{}/prebuilt/third_party/clang/{}/bin/llvm-profdata'.format(
                fuchsia_dir, self.host.platform))
        self.host.mkdir(build_dir + '/.build-id')
        self.host.mkdir(build_dir + '.zircon/.build-id')
        self.host.touch(build_dir + '/ssh-keys/ssh_config')
        buildenv.configure(build_dir)
        golden = 'data/v2.fuzzers.json'
        self.host.add_golden(golden)
        buildenv.read_fuzzers(golden)
        self._buildenv = buildenv
        return self._buildenv

    @property
    def device(self):
        """The associated Device object."""
        if self._device:
            return self._device
        device = Device(self, addr='::1')
        device.configure()
        self._device = device
        return self._device
