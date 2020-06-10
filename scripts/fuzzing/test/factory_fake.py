#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import test_env
from lib.factory import Factory

from cli_fake import FakeCLI
from host_fake import FakeHost
from device_fake import FakeDevice
from fuzzer_fake import FakeFuzzer


class FakeFactory(Factory):
    """Fake factory that associates fake objects.

       Unlike the real factory, this object caches and reuses created hosts and
       devices. It also allows tests to access created objects for examination.

       Attributes:
         host:          The associated FakeHost object.
         device:        The associated FakeDevice object.
         fuzzer:        The most recently created FakeFuzzer object.
    """

    def __init__(self):
        cli = FakeCLI()
        super(FakeFactory, self).__init__(cli)
        self._host = None
        self._device = None
        self._fuzzer = None

    @property
    def host(self):
        """The associated FakeHost object."""
        return self.create_host()

    @property
    def device(self):
        """The associated FakeDevice object."""
        return self.create_device()

    @property
    def fuzzer(self):
        """The most recently created FakeFuzzer object."""
        if not self._fuzzer:
            raise RuntimeError('Fuzzer not set.')
        return self._fuzzer

    def create_host(self):
        """Returns the factory's fake host, creating it if needed."""
        if not self._host:
            self._host = FakeHost(cli=self._cli)
        return self._host

    def create_device(self):
        """Returns the factory's fake device, creating it if needed."""
        if not self._device:
            self._device = FakeDevice(host=self.host)
        return self._device

    def create_fuzzer(self, args):
        """Returns a fake fuzzer using the factory's fake host and device."""
        self._fuzzer = self._create_fuzzer_impl(FakeFuzzer, self.device, args)
        return self._fuzzer
