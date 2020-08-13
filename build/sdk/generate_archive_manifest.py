#!/usr/bin/env python3.8
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys

try:
  # Python 3
  from urllib.parse import urlparse
except ImportError:
  from urlparse import urlparse

from sdk_common import Atom


class MappingAction(argparse.Action):
    '''Parses file mappings flags.'''

    def __init__(self, option_strings, dest, nargs=None, **kwargs):
        if nargs is not None:
            raise ValueError("nargs is not allowed")
        super(MappingAction, self).__init__(
            option_strings, dest, nargs=2, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        mappings = getattr(namespace, 'mappings', None)
        if mappings is None:
            mappings = {}
            setattr(namespace, 'mappings', mappings)
        mappings[values[0]] = values[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--manifest', help='Path to the SDK\'s manifest file', required=True)
    parser.add_argument(
        '--mapping',
        help='Extra files to add to the archive',
        action=MappingAction)
    parser.add_argument(
        '--output', help='Path to the output file manifest', required=True)
    args = parser.parse_args()

    with open(args.manifest, 'r') as manifest_file:
        manifest = json.load(manifest_file)

    all_files = {}

    def add(dest_path, src_path):
        dest = os.path.normpath(dest_path)
        src = os.path.normpath(src_path)
        if dest in all_files:
            print('Error: multiple entries for %s' % dest)
            print('  - %s' % all_files[dest])
            print('  - %s' % src)
            return 1
        all_files[dest] = src

    for atom in [Atom(a) for a in manifest['atoms']]:
        for file in atom.files:
            add(file.destination, file.source)

    for dest, source in args.mappings.items():
        add(dest, source)

    with open(args.output, 'w') as output_file:
        for mapping in sorted(all_files.items()):
            output_file.write('%s=%s\n' % mapping)


if __name__ == '__main__':
    sys.exit(main())
