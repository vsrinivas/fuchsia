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
      description='Reports status for the fuzzer matching NAME if provided, ' +
      'or for all running fuzzers.  Status includes execution state, corpus ' +
      'size, and number of artifacts.',
      name_required=False)
  args = parser.parse_args()

  host = Host()
  device = Device.from_args(host, args)
  fuzzers = Fuzzer.filter(host.fuzzers, args.name)

  pids = device.getpids()
  silent = True
  for pkg, tgt in fuzzers:
    fuzzer = Fuzzer(device, pkg, tgt)
    if not args.name and tgt not in pids:
      continue
    silent = False
    if tgt in pids:
      print(str(fuzzer) + ': RUNNING')
    else:
      print(str(fuzzer) + ': STOPPED')
    print('    Output path:  ' + fuzzer.data_path())
    print('    Corpus size:  %d inputs / %d bytes' % fuzzer.measure_corpus())
    artifacts = fuzzer.list_artifacts()
    if len(artifacts) == 0:
      print('    Artifacts:    None')
    else:
      print('    Artifacts:    ' + artifacts[0])
      for artifact in artifacts[1:]:
        print('                  ' + artifact)
  if silent:
    print(
        'No fuzzers are running.  Include \'name\' to check specific fuzzers.')
    return 1
  return 0


if __name__ == '__main__':
  sys.exit(main())
