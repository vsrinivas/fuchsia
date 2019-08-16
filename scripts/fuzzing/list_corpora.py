#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.args import Args
from lib.cipd import Cipd
from lib.corpus import Corpus
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
    parser = Args.make_parser(
        description='Lists corpus instances in CIPD for the named fuzzer')
    args = parser.parse_args()

    host = Host.from_build()
    device = Device.from_args(host, args)
    fuzzer = Fuzzer.from_args(device, args)

    with Corpus.from_args(fuzzer, args) as corpus:
        cipd = Cipd(corpus)
        print(cipd.instances())
    return 0


if __name__ == '__main__':
    sys.exit(main())
