#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.host import Host
from lib.fuzzer import Fuzzer


def main():
  parser = Fuzzer.make_parser(
      description='Lists fuzzers matching NAME if provided, or all fuzzers.',
      name_required=False)
  parser.add_argument('-d', '--device', help=argparse.SUPPRESS)
  args = parser.parse_args()

  host = Host()

  fuzzers = Fuzzer.filter(host.fuzzers, args.name)
  if len(fuzzers) == 0:
    print('No matching fuzzers.')
  else:
    print('Found %d matching fuzzers:' % len(fuzzers))
    for fuzzer in fuzzers:
      print('  %s/%s' % fuzzer)


if __name__ == '__main__':
  sys.exit(main())
