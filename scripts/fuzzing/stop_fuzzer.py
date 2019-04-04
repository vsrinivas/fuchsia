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
  parser = Fuzzer.make_parser('Stops the named fuzzer.')
  args = parser.parse_args()

  host = Host()
  device = Device.from_args(host, args)
  fuzzer = Fuzzer.from_args(device, args)

  fuzzer.stop()
  print('Stopping ' + str(fuzzer) + '.')


if __name__ == '__main__':
  sys.exit(main())
