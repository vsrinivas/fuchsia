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
        '-n',
        '--no-cipd',
        action='store_true',
        help='Skip steps which involve transferring packages to or from CIPD')
    parser.add_argument(
        '-o', '--output', help='Path under which to store results.')
    parser.add_argument(
        '-s',
        '--staging',
        help='Host directory to use for un/packing corpus bundles.' +
        ' Defaults to a temporary directory.')
    name_help = ('Fuzzer name to match.  This can be part of the package and/or'
                 + ' target name, e.g. "foo", "bar", and "foo/bar" all match' +
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
          'Otherwise installs the labeled version from CIPD. In this case, ' +
          '"label" may be either a "ref" or a key:value "tag"  as described ' +
          'in `cipd help`. By default, corpora are uploaded with the ' +
          '"latest" ref and a tag of "integration:<git-revision>" ' +
          'corresponding to current revision of the //integration repository.')
    return parser
