#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.args import Args
from lib.host import Host
from lib.device import Device
from lib.fuzzer import Fuzzer


def main():
  parser = Args.make_parser(
      'Minimizes the current corpus for the named fuzzer. This should be ' +
      'used after running the fuzzer for a while, or after incorporating a ' +
      'third-party corpus using \'fetch-corpus\'')
  args, fuzzer_args = parser.parse_known_args()

  host = Host()
  device = Device.from_args(host, args)
  fuzzer = Fuzzer.from_args(device, args)

  fuzzer.merge(fuzzer_args)
  return 0


if __name__ == '__main__':
  sys.exit(main())
