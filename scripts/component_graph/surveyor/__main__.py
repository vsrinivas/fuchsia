#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tool to dump the v1 component graph in CSV form.

This tool produces a dump of the component graph in CSV form. Aside from the
component dependencies, the output contains some component information that is
relevant to the Component Framework migration.
"""

import argparse
import csv
from server.fpm import PackageManager
import server.util.env
from surveyor.util import Surveyor
import sys

def main(args):
  fuchsia_root = server.util.env.get_fuchsia_root()
  package_manager = PackageManager(args.package_server, fuchsia_root)
  packages = package_manager.get_packages()
  services = package_manager.get_services(packages)
  groups_json = Surveyor.load_groups_json(fuchsia_root)
  surveyor = Surveyor(groups_json)
  tuples = surveyor.export(packages, services)
  f = sys.stdout if args.out_csv == '' else open(args.out_csv, 'w+', newline='')
  with f:
    writer = csv.writer(f, dialect='excel')
    for tup in tuples:
      writer.writerow(tup)

if __name__ == '__main__':
  arg_parser = argparse.ArgumentParser(
      description='Fuchsia component surveyor.')
  arg_parser.add_argument(
      'out_csv',
      type=str,
      nargs='?',
      default='',
      help='CSV file to write results to')
  arg_parser.add_argument(
      '--package-server',
      type=str,
      default='http://0.0.0.0:8083',
      help='Package server to get packages from')
  main(arg_parser.parse_args())
