#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import subprocess
import sys

from lib.args import Args
from lib.cipd import Cipd
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
  parser = Args.make_parser(
      'Transfers a fuzzing corpus for a named fuzzer from a device to CIPD')
  args = parser.parse_args()

  host = Host.from_build()
  device = Device.from_args(host, args)
  fuzzer = Fuzzer.from_args(device, args)

  if fuzzer.measure_corpus()[0] == 0:
    print('Ignoring ' + str(fuzzer) + '; corpus is empty.')
    return 0
  with Cipd.from_args(fuzzer, args) as cipd:
    device.fetch(fuzzer.data_path('corpus/*'), cipd.root)
    if not cipd.create():
      return 1
  return 0


if __name__ == '__main__':
  sys.exit(main())
