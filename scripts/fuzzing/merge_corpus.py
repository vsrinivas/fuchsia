#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

from lib.args import Args
from lib.cipd import Cipd
from lib.corpus import Corpus
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
    parser = Args.make_parser(
        'Minimizes the current corpus for the named fuzzer. This should be ' +
        'used after running the fuzzer for a while, or after incorporating a ' +
        'third-party corpus using \'fetch-corpus\'')
    args, fuzzer_args = parser.parse_known_args()

    host = Host.from_build()
    device = Device.from_args(host, args)
    fuzzer = Fuzzer.from_args(device, args)

    with Corpus.from_args(fuzzer, args) as corpus:
        cipd = Cipd(corpus)
        if not args.no_cipd:
            cipd.install('latest')
        corpus.push()
        if fuzzer.merge(fuzzer_args) == (0, 0):
            print('Corpus for ' + str(fuzzer) + ' is empty.')
            return 1
        corpus.pull()
        if not args.no_cipd:
            cipd.create()
    return 0


if __name__ == '__main__':
    sys.exit(main())
