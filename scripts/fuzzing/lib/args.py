#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import re
import sys

from host import Host


class ArgParser:

    def __init__(self, description):
        """ Create an argument parser for the described command."""
        self._description = description
        self._name_required = True
        self._reset()

    def require_name(self, required):
        """ Sets whether the command requires a 'name' argument."""
        self._name_required = required
        self._reset()

    def _reset(self):
        """ Rebuilds the underlying ArgumentParser. """
        self._parser = argparse.ArgumentParser(description=self._description)

        # Positional arguments
        name_help = (
            'Fuzzer name to match.  This can be part of the package and/or ' +
            'target name, e.g. "foo", "bar", and "foo/bar" all match ' +
            '"foo_package/bar_target".')
        if self._name_required:
            self._parser.add_argument('name', help=name_help)
        else:
            self._parser.add_argument('name', nargs='?', help=name_help)

        # Flags
        self._parser.add_argument(
            '--foreground',
            action='store_true',
            help='Displays fuzzer output. Implied for \'repro\' and \'merge\'.')
        self._parser.add_argument(
            '--debug',
            action='store_true',
            help='If true, disable exception handling in libFuzzer.')
        self._parser.add_argument(
            '--output', help='Path under which to store results.')
        self._parser.add_argument(
            '--monitor', action='store_true', help=argparse.SUPPRESS)
        self._parser.add_argument(
            'libfuzzer_inputs', nargs='*', help='Inputs to libFuzzer.')

    def parse(self, args=None):
        """ Parses arguments for fx fuzz, libFuzzer, and the fuzzer.

        This will distribute arguments into four categories:
        1. Arguments and flags described above are for this process.
        2. Arguments of the form "-key=val" are libFuzzer options.
        3. Remaining arguments before '--' are libFuzzer positional arguments.
        4. Arguments after '--'' are for the fuzzer subprocess.

        Standard argument parsing via `argparse.parse_known_args` can
        distinguish between categories 1 and 2-4, but to further separate them
        this method must do additional parsing.

        Args:
            A list of command line arguments. If None, sys.argv is used.

        Returns:
            A tuple consisting of:
                1. An argparse-populated namespace
                2. A dict of libFuzzer options mapping keys to values.
                3. A list of libFuzzer inputs.
                4. A list of fuzzer subprocess arguments.
        """
        libfuzzer_opt_re = re.compile(r'-(\w+)=(.*)')
        long_opt_re = re.compile(r'--\w+')
        positional_re = re.compile(r'[^-].*')
        other = []
        libfuzzer_opts = {}
        subprocess_args = []
        pass_to_subprocess = False
        if not args:
            args = sys.argv[1:]
        for arg in args:
            libfuzzer_opt = libfuzzer_opt_re.match(arg)
            long_opt = long_opt_re.match(arg)
            positional = positional_re.match(arg)
            if pass_to_subprocess:
                subprocess_args.append(arg)
            elif arg == '--':
                pass_to_subprocess = True
            elif libfuzzer_opt:
                libfuzzer_opts[libfuzzer_opt.group(1)] = libfuzzer_opt.group(2)
            elif long_opt or positional:
                other.append(arg)
            else:
                self._parser.error('unrecognized option: {}'.format(arg))
        args = self._parser.parse_args(other)
        args.libfuzzer_opts = libfuzzer_opts
        args.subprocess_args = subprocess_args
        return args
