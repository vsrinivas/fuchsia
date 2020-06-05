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
        'Transfers corpus for a named fuzzer to a device. By default, copies ' +
        'the latest corpus from CIPD. To copy a local directory instead, use ' +
        'the --staging and --no-cipd options.')
    parser.allow_label(True)
    args = parser.parse_args()

    host = Host.from_build()
    device = Device.from_host(host)
    fuzzer = Fuzzer.from_args(device, args)

    with Corpus.from_args(fuzzer, args) as corpus:
        cipd = Cipd(corpus)
        if not args.no_cipd:
            cipd.install(args.label)
        corpus.push()
    return 0


if __name__ == '__main__':
    sys.exit(main())
