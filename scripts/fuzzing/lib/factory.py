#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno

from args import ArgParser
from host import Host
from buildenv import BuildEnv
from device import Device
from fuzzer import Fuzzer


class Factory(object):
    """Facility for creating associated objects.

    The factory can create Hosts, BuildEnvs, Devices, and
    Fuzzers. More importantly, it can construct them with references to
    each other, i.e. a Factory-constructed Fuzzer automatically gets a
    reference to a Factory-constructed Device, which has a reference to a
    Factory-constructed BuildEnv.

    Attributes:
        host:           System interface object for user interactions.
        buildenv:       The associated BuildEnv object.
        device:         The associated Device object.
    """

    def __init__(self, host=None):
        if not host:
            host = Host()
        self._parser = None
        self._host = host
        self._buildenv = None
        self._device = None

    @property
    def host(self):
        """System interface object for user interactions."""
        return self._host

    @property
    def parser(self):
        """The associated ArgParser object."""
        if self._parser:
            return self._parser
        parser = ArgParser()
        parser.host = self.host
        parser.add_parsers()
        self._parser = parser
        return self._parser

    @property
    def buildenv(self):
        """The associated BuildEnv object."""
        if self._buildenv:
            return self._buildenv
        buildenv = BuildEnv(self)
        pathname = buildenv.abspath('//.fx-build-dir')
        build_dir = '//' + self.host.readfile(
            pathname,
            on_error=[
                'Failed to read build directory from {}.'.format(pathname),
                'Have you run "fx set ... --fuzz-with <sanitizer>"?'
            ])
        buildenv.configure(build_dir)
        buildenv.read_fuzzers(buildenv.abspath(build_dir, 'fuzzers.json'))
        self._buildenv = buildenv
        return self._buildenv

    @property
    def device(self):
        """The associated Device object."""
        if self._device:
            return self._device
        pathname = '{}.device'.format(self.buildenv.build_dir)
        device_name = self.host.readfile(pathname, missing_ok=True)
        device = Device(self, name=device_name)
        device.configure()
        self._device = device
        return self._device

    def create_fuzzer(self, args, include_tests=False):
        """Constructs a Fuzzer from command line arguments, showing a
        disambiguation menu if specified name matches more than one fuzzer."""
        fuzzers = self.buildenv.fuzzers(args.name, include_tests=include_tests)
        if not fuzzers:
            self.host.error('No matching fuzzers found.', 'Try "fx fuzz list".')
        if len(fuzzers) > 1:
            choices = {}
            for fuzzer in fuzzers:
                choices[str(fuzzer)] = fuzzer
            self.host.echo('More than one match found.')
            prompt = 'Please pick one from the list'
            choice = self.host.choose(prompt, sorted(choices.keys()))
            fuzzer = choices[choice]
        else:
            fuzzer = fuzzers[0]
        fuzzer.update(args)
        return fuzzer
