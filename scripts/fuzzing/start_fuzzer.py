#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.args import Args
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
  parser = Args.make_parser(
      'Starts the named fuzzer.  Additional arguments are passed through.')
  args, fuzzer_args = parser.parse_known_args()

  host = Host()
  device = Device.from_args(host, args)
  fuzzer = Fuzzer.from_args(device, args)

  print('Starting ' + str(fuzzer) + '.')
  print('Outputs will be written to ' + fuzzer.results())
  if not args.foreground:
    print('You should be notified when the fuzzer stops.')
    print('To check its progress, use `fx fuzz check ' + str(fuzzer) + '`.')
    print('To stop it manually, use `fx fuzz stop ' + str(fuzzer) + '`.')
  fuzzer.start(fuzzer_args)

  title = str(fuzzer) + ' has stopped.'
  body = 'Output written to ' + fuzzer.results() + '.'
  print(title)
  print(body)
  host.notify_user(title, body)
  return 0


if __name__ == '__main__':
  sys.exit(main())
