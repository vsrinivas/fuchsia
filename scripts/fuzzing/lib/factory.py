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
        host:   System interface object for user interactions.
    """

    def __init__(self, host=None):
        if not host:
            host = Host()
        self._host = host

    @property
    def host(self):
        """System interface object for user interactions."""
        return self._host

    def create_parser(self):
        """Returns an argument parser."""
        parser = ArgParser()
        parser.host = self.host
        parser.add_parsers()
        return parser

    def create_buildenv(self, fuchsia_dir=None):
        """Constructs a BuildEnv from a local build directory."""
        if not fuchsia_dir:
            fuchsia_dir = self.host.getenv('FUCHSIA_DIR')
        if not fuchsia_dir:
            self.host.error(
                'FUCHSIA_DIR not set.', 'Have you sourced "scripts/fx-env.sh"?')
        buildenv = BuildEnv(self.host, fuchsia_dir)
        pathname = buildenv.path('.fx-build-dir')
        build_dir = self.host.readfile(
            pathname,
            on_error=[
                'Failed to read build directory from {}.'.format(pathname),
                'Have you run "fx set ... --fuzz-with <sanitizer>"?'
            ])
        buildenv.configure(build_dir)
        buildenv.read_fuzzers(buildenv.path(build_dir, 'fuzzers.json'))
        return buildenv

    def create_device(self, buildenv=None):
        """Constructs a Device from the build environment"""
        if not buildenv:
            buildenv = self.create_buildenv()
        pathname = '{}.device'.format(buildenv.build_dir)
        device_name = self.host.readfile(pathname, missing_ok=True)
        addr = buildenv.find_device(device_name)
        device = Device(buildenv, addr)
        device.configure()
        return device

    def _resolve_fuzzer(self, buildenv, name):
        """Matches a fuzzer name pattern to a fuzzer."""
        matches = buildenv.fuzzers(name)
        if not matches:
            self.host.error('No matching fuzzers found.', 'Try "fx fuzz list".')
        if len(matches) > 1:
            choices = ["/".join(m) for m in matches]
            self.host.echo('More than one match found.')
            prompt = 'Please pick one from the list:'
            return self.host.choose(prompt, choices).split('/')
        else:
            return matches[0]

    def create_fuzzer(self, args, device=None):
        """Constructs a Fuzzer from command line arguments, showing a
        disambiguation menu if specified name matches more than one fuzzer."""
        if not device:
            device = self.create_device()

        package, executable = self._resolve_fuzzer(device.buildenv, args.name)
        fuzzer = Fuzzer(device, package, executable)

        keys = [
            key for key, val in vars(Fuzzer).items()
            if isinstance(val, property) and val.fset
        ]
        for key, val in vars(args).items():
            if key in keys and val is not None:
                setattr(fuzzer, key, val)

        return fuzzer
