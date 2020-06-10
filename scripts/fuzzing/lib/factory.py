#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import re
import os

from cli import CommandLineInterface
from host import Host
from device import Device
from fuzzer import Fuzzer


class Factory(object):
    """Facility for creating associated objects.

       The factory can create CommandLineInterfaces, Hosts, Devices, and
       Fuzzers. More importantly, it can construct them with references to
       each other, i.e. a Factory-constructed Fuzzer automatically gets a
       reference to a Factory-constructed Device, which has a reference to a
       Factory-constructed Host, which may have a reference to a Factory-
       constructed CommandLineInterface.

       Attributes:
         cli:       Command line interface object for user interactions.
    """

    def __init__(self, cli=None):
        self._cli = cli

    @property
    def cli(self):
        if not self._cli:
            self._cli = CommandLineInterface()
        return self._cli

    def create_host(self, fuchsia_dir=os.getenv('FUCHSIA_DIR')):
        """Constructs a Host from a local build directory."""
        if not fuchsia_dir:
            self.cli.error(
                'FUCHSIA_DIR not set.', 'Have you sourced "scripts/fx-env.sh"?')
        host = Host(self.cli, fuchsia_dir)
        try:
            with open(host.fxpath('.fx-build-dir')) as opened:
                build_dir = opened.read().strip()
            with open(host.fxpath(build_dir, 'fuzzers.json')) as opened:
                host.configure(build_dir, opened)
        except IOError as e:
            if e.errno == errno.ENOENT:
                self.cli.error(
                    'Initialization failure.',
                    'Have you run `fx set ... --fuzz-with <sanitizer>`?')
            else:
                raise
        return host

    def create_device(self, host=None):
        """Constructs a Device from the build environment"""
        if not host:
            host = self.create_host()
        addr = None
        try:
            with open('{}.device'.format(host.build_dir)) as opened:
                addr = host.find_device(device_file=opened)
        except IOError as e:
            if e.errno != errno.ENOENT:
                raise
        if not addr:
            addr = host.find_device()
        device = Device(host, addr)
        device.configure()
        return device

    def _resolve_fuzzer(self, host, name):
        """Matches a fuzzer name pattern to a fuzzer."""
        matches = host.fuzzers(name)
        if not matches:
            self.cli.error('No matching fuzzers found.', 'Try `fx fuzz list`.')
        if len(matches) > 1:
            choices = ["/".join(m) for m in matches]
            self.cli.echo('More than one match found.')
            prompt = 'Please pick one from the list:'
            return self.cli.choose(prompt, choices).split('/')
        else:
            return matches[0]

    def create_fuzzer(self, args, device=None):
        """Constructs a Fuzzer from command line arguments, showing a
        disambiguation menu if specified name matches more than one fuzzer."""
        if not device:
            device = self.create_device()
        return self._create_fuzzer_impl(Fuzzer, device, args)

    def _create_fuzzer_impl(self, cls, device, args):
        """Creates a Fuzzer-like object of the given class."""
        package, executable = self._resolve_fuzzer(device.host, args.name)
        fuzzer = cls(device, package, executable)
        if args.output:
            fuzzer.output = args.output
        fuzzer.foreground = args.foreground
        fuzzer.debug = args.debug
        fuzzer.libfuzzer_opts = args.libfuzzer_opts
        fuzzer.libfuzzer_inputs = args.libfuzzer_inputs
        fuzzer.subprocess_args = args.subprocess_args
        return fuzzer
