#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

from lib.args import ArgParser
from lib.cipd import Cipd
from lib.corpus import Corpus
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
    parser = ArgParser(
        'Minimizes the current corpus for the named fuzzer. This should be ' +
        'used after running the fuzzer for a while, or after incorporating a ' +
        'third-party corpus using \'fetch-corpus\'')
    args, libfuzzer_opts, libfuzzer_args, subprocess_args = parser.parse()

    host = Host.from_build()
    device = Device.from_host(host)
    fuzzer = Fuzzer.from_args(device, args)
    fuzzer.libfuzzer_opts = libfuzzer_opts
    fuzzer.libfuzzer_args = libfuzzer_args
    fuzzer.subprocess_args = subprocess_args

    with Corpus.from_args(fuzzer, args) as corpus:
        cipd = Cipd(corpus)
        if not args.no_cipd:
            cipd.install('latest')
        corpus.push()
        if fuzzer.merge() == (0, 0):
            print('Corpus for ' + str(fuzzer) + ' is empty.')
            return 1
        corpus.pull()
        if not args.no_cipd:
            cipd.create()
    return 0


if __name__ == '__main__':
    sys.exit(main())
