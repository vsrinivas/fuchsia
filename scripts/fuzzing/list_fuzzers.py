#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.args import Args
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
  parser = Args.make_parser(
      description='Lists fuzzers matching NAME if provided, or all fuzzers.',
      name_required=False)
  args = parser.parse_args()

  host = Host.from_build()

  fuzzers = Fuzzer.filter(host.fuzzers, args.name)
  if len(fuzzers) == 0:
    print('No matching fuzzers.')
    return 1
  print('Found %d matching fuzzers:' % len(fuzzers))
  for fuzzer in fuzzers:
    print('  %s/%s' % fuzzer)
  return 0


if __name__ == '__main__':
  sys.exit(main())
