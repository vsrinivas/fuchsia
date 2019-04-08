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
  parser = Args.make_parser('Stops the named fuzzer.')
  args = parser.parse_args()

  host = Host()
  device = Device.from_args(host, args)
  fuzzer = Fuzzer.from_args(device, args)

  if fuzzer.is_running():
    print('Stopping ' + str(fuzzer) + '.')
    fuzzer.stop()
  else:
    print(str(fuzzer) + ' is already stopped.')
  return 0


if __name__ == '__main__':
  sys.exit(main())
