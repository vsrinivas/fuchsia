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
      'Transfers corpus for a named fuzzer to a device', label_present=True)
  args = parser.parse_args()

  host = Host()
  device = Device.from_args(host, args)
  fuzzer = Fuzzer.from_args(device, args)

  if os.path.isdir(args.label):
    device.store(os.path.join(args.label, '*'), fuzzer.data_path('corpus'))
    return 0
  with Cipd.from_args(fuzzer, args, label=args.label) as cipd:
    if not cipd.install():
      return 1
    device.store(os.path.join(cipd.root, '*'), fuzzer.data_path('corpus'))
  return 0


if __name__ == '__main__':
  sys.exit(main())
