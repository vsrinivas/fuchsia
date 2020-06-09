#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.args import ArgParser
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
    parser = ArgParser(
        'Runs the named fuzzer on provided test units, or all current test ' +
        'units for the fuzzer. Use \'check-fuzzer\' to see current tests units.'
    )
    args, libfuzzer_opts, libfuzzer_args, subprocess_args = parser.parse()

    host = Host.from_build()
    device = Device.from_host(host)
    fuzzer = Fuzzer.from_args(device, args)
    fuzzer.libfuzzer_opts = libfuzzer_opts
    fuzzer.libfuzzer_args = libfuzzer_args
    fuzzer.subprocess_args = subprocess_args

    if fuzzer.repro() == 0:
        print('No matching artifacts found.')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
