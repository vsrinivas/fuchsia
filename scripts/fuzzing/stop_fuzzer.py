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
from lib.cli import CommandLineInterface


def main():
    cli = CommandLineInterface()
    parser = ArgParser(cli, 'Stops the named fuzzer.')
    args = parser.parse_args()

    host = Host.from_build(cli)
    device = Device.from_host(host)
    fuzzer = Fuzzer.from_args(device, args)

    if fuzzer.is_running():
        cli.echo('Stopping ' + str(fuzzer) + '.')
        fuzzer.stop()
    else:
        cli.echo(str(fuzzer) + ' is already stopped.')


if __name__ == '__main__':
    sys.exit(main())
