#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import unittest

import test_env
import lib.command as command
from lib.host import Host
from lib.factory import Factory
from test_case import TestCaseWithIO


class IntegrationTest(TestCaseWithIO):

    def assertNoErrors(self):
        """Convenience method to reset stdout and assert stderr is empty."""
        self.assertOut([], n=0)
        self.assertErr([])

    def test_e2e(self):
        # Set up hermetic environment.
        host = Host()
        host.fd_out = self._stdout
        host.fd_err = self._stderr

        with host.temp_dir() as temp_dir:

            # (Re-)parse the command line arguments, a la main.py.
            factory = Factory(host=host)
            parser = factory.parser
            args = parser.parse_args()

            # Ensure exactly 1 fuzzer is selected.
            fuzzer = factory.create_fuzzer(args)
            self.assertNoErrors()
            args.name = str(fuzzer)

            list_args = parser.parse_args(['list', args.name])
            list_args.command(list_args, factory)
            self.assertOut(
                ['Found 1 matching fuzzer for "{}":'.format(str(fuzzer))], n=1)
            self.assertNoErrors()

            start_args = parser.parse_args(
                ['start', '-o', temp_dir.pathname, args.name])
            proc = command.start_fuzzer(start_args, factory)
            self.assertNoErrors()

            stop_args = parser.parse_args(['stop', args.name])
            command.stop_fuzzer(stop_args, factory)
            self.assertNoErrors()
            if proc:
                proc.wait()

            check_args = parser.parse_args(['check', args.name])
            command.check_fuzzer(check_args, factory)
            self.assertOut(['{}: STOPPED'.format(args.name)], n=1)
            self.assertNoErrors()

            unit = os.path.join(temp_dir.pathname, 'unit')
            with open(unit, 'w') as opened:
                opened.write('hello world')

            repro_args = parser.parse_args(['repro', args.name, unit])
            command.repro_units(repro_args, factory)
            self.assertNoErrors()

            analyze_args = ['analyze', '-max_total_time=10', args.name]
            if args.local:
                analyze_args.append('--local')
            analyze_args = parser.parse_args(analyze_args)
            command.analyze_fuzzer(analyze_args, factory)
            self.assertNoErrors()


if __name__ == '__main__':
    unittest.main()
