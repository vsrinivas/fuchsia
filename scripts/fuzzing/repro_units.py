#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

import lib.command as command
from lib.args import ArgParser
from lib.factory import Factory


def main():
    factory = Factory()
    parser = ArgParser(
        factory.cli,
        'Runs the named fuzzer on provided test units, or all current test ' +
        'units for the fuzzer. Use \'check-fuzzer\' to see current tests units.'
    )
    command.repro_units(parser.parse(), factory=factory)


if __name__ == '__main__':
    sys.exit(main())
