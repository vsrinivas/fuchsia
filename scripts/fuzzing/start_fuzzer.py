#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
  parser = Fuzzer.make_parser(
      'Starts the named fuzzer.  Additional arguments are passed through.')
  parser.add_argument(
      '-o',
      '--out',
      action='store',
      help='Path under which to store results.')
  parser.add_argument(
      '-v',
      '--verbose',
      action='store_true',
      help='If true, display fuzzer output.')
  args, fuzzer_args = parser.parse_known_args()

  host = Host()
  device = Device.from_args(host, args)
  fuzzer = Fuzzer.from_args(device, args)

  fuzzer.prepare(args.out)
  print('Starting ' + str(fuzzer) + '.')
  print('Outputs will be written to ' + fuzzer.results())
  if not args.verbose:
    print('You should be notified when the fuzzer stops.')
    print('To check its progress, use `fx fuzz check ' + str(fuzzer) + '`.')
    print('To stop it manually, use `fx fuzz stop ' + str(fuzzer) + '`.')
  fuzzer.start(fuzzer_args, args.verbose)

  title = str(fuzzer) + ' has stopped.'
  body = 'Output written to ' + fuzzer.results() + '.'
  print(title)
  print(body)
  host.notify_user(title, body)


if __name__ == '__main__':
  sys.exit(main())
