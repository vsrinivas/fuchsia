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
from lib.factory import Factory


def main():
    factory = Factory()
    parser = ArgParser(factory.cli, 'Stops the named fuzzer.')
    args = parser.parse()

    cli = factory.cli
    fuzzer = factory.create_fuzzer(args)
    if fuzzer.is_running():
        cli.echo('Stopping ' + str(fuzzer) + '.')
        fuzzer.stop()
    else:
        cli.echo(str(fuzzer) + ' is already stopped.')


if __name__ == '__main__':
    sys.exit(main())
