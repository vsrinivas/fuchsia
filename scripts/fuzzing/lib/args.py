#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse

from host import Host


class Args:

  @classmethod
  def make_parser(cls, description, name_required=True, label_present=False):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument(
        '-d',
        '--device',
        help='Name of device, only needed when multiple devices are present.')
    parser.add_argument(
        '-f',
        '--foreground',
        action='store_true',
        help='If true, display fuzzer output.')
    parser.add_argument(
        '-o', '--output', help='Path under which to store results.')
    parser.add_argument(
        '-s',
        '--staging',
        help='Host directory to use for un/packing corpus bundles.' +
        ' Defaults to a temporary directory.')
    name_help = (
        'Fuzzer name to match.  This can be part of the package and/or' +
        ' target name, e.g. "foo", "bar", and "foo/bar" all match' +
        ' "foo_package/bar_target".')
    if name_required:
      parser.add_argument('name', help=name_help)
    else:
      parser.add_argument('name', nargs='?', help=name_help)
    if label_present:
      parser.add_argument(
          'label',
          nargs='?',
          default='latest',
          help='If a directory, installs a corpus from that location. ' +
          'Otherwise installs the labeled version from CIPD.')
    return parser
