#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.args import ArgParser
from lib.fuzzer import Fuzzer
from lib.host import Host
from lib.factory import Factory


def main():
    factory = Factory()
    parser = ArgParser(
        factory.cli, 'Lists fuzzers matching NAME if provided, or all fuzzers.')
    parser.require_name(False)
    args = parser.parse()

    cli = factory.cli
    host = factory.create_host()
    fuzzers = host.fuzzers(args.name)
    if len(fuzzers) == 0:
        cli.error('No matching fuzzers.')
    cli.echo('Found %d matching fuzzers:' % len(fuzzers))
    for fuzzer in fuzzers:
        cli.echo('  %s/%s' % fuzzer)


if __name__ == '__main__':
    sys.exit(main())
