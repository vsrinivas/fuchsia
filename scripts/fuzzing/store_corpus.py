#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import subprocess
import sys

from lib.args import ArgParser
from lib.cipd import Cipd
from lib.corpus import Corpus
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
    parser = ArgParser(
        'Transfers a fuzzing corpus for a named fuzzer from a device to CIPD')
    args = parser.parse_args()

    host = Host.from_build()
    device = Device.from_host(host)
    fuzzer = Fuzzer.from_args(device, args)

    if fuzzer.measure_corpus()[0] == 0:
        print('Ignoring ' + str(fuzzer) + '; corpus is empty.')
        return 0
    with Corpus.from_args(fuzzer, args) as corpus:
        corpus.pull()
        cipd = Cipd(corpus)
        if not args.no_cipd:
            cipd.create()
    return 0


if __name__ == '__main__':
    sys.exit(main())
