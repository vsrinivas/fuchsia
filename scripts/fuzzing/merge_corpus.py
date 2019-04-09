#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

from lib.args import Args
from lib.cipd import Cipd
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
  parser = Args.make_parser(
      'Minimizes the current corpus for the named fuzzer. This should be ' +
      'used after running the fuzzer for a while, or after incorporating a ' +
      'third-party corpus using \'fetch-corpus\'')
  args, fuzzer_args = parser.parse_known_args()

  host = Host()
  device = Device.from_args(host, args)
  fuzzer = Fuzzer.from_args(device, args)

  with Cipd.from_args(fuzzer, args) as cipd:
    if cipd.install():
      device.store(os.path.join(cipd.root, '*'), fuzzer.data_path('corpus'))
    if fuzzer.merge(fuzzer_args) == (0, 0):
      print('Corpus for ' + str(fuzzer) + ' is empty.')
      return 1
    device.fetch(fuzzer.data_path('corpus/*'), cipd.root)
    if not cipd.create():
      return 1
  return 0


if __name__ == '__main__':
  sys.exit(main())
