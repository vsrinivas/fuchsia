#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

from lib.cipd import Cipd
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
  parser = Cipd.make_parser('Transfers corpus for a named fuzzer to a device')
  parser.add_argument(
      '-c',
      '--corpus',
      action='store',
      help='Optional location to copy the corpus from instead of CIPD.' +
      ' This can be used to install third-party corpora that can be' +
      ' subsequently merged and stored in CIPD.'
  )
  parser.add_argument(
      '-l',
      '--label',
      action='store',
      default='latest',
      help='Labeled version to retrieve from CIPD.' +
      ' Ignored if \'--corpus\' is provided.'
  )
  args = parser.parse_args()

  host = Host()
  device = Device.from_args(host, args)
  fuzzer = Fuzzer.from_args(device, args)

  if args.corpus:
    device.store(os.path.join(args.corpus, '*'), fuzzer.data_path('corpus'))
    return
  with Cipd.from_args(fuzzer, args, label=args.label) as cipd:
    if not cipd.list():
      print 'No corpus instances found in CIPD for ' + str(fuzzer)
      return
    cipd.install()
    device.store(os.path.join(cipd.root, '*'), fuzzer.data_path('corpus'))


if __name__ == '__main__':
  sys.exit(main())
