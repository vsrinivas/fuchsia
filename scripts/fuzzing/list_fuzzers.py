#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.args import ArgParser
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
    parser = ArgParser(
        'Lists fuzzers matching NAME if provided, or all fuzzers.')
    parser.require_name(False)
    args = parser.parse_args()

    host = Host.from_build()
    fuzzers = host.fuzzers(args.name)
    if len(fuzzers) == 0:
        print('No matching fuzzers.')
        return 1
    print('Found %d matching fuzzers:' % len(fuzzers))
    for fuzzer in fuzzers:
        print('  %s/%s' % fuzzer)
    return 0


if __name__ == '__main__':
    sys.exit(main())
