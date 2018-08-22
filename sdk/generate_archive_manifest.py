#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys
from urlparse import urlparse

from sdk_common import Atom


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifest',
                        help='Path to the SDK\'s manifest file',
                        required=True)
    parser.add_argument('--meta',
                        help='Path to SDK metadata file',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the output file manifest',
                        required=True)
    args = parser.parse_args()

    with open(args.manifest, 'r') as manifest_file:
        manifest = json.load(manifest_file)

    all_files = {}
    def add(dest, src):
        if dest in all_files:
            print('Error: multiple entries for %s' % dest)
            print('  - %s' % all_files[dest])
            print('  - %s' % src)
            return 1
        all_files[dest] = src

    for atom in [Atom(a) for a in manifest['atoms']]:
        if atom.new_files:
            for file in atom.new_files:
                add(file.destination, file.source)
        # TODO(DX-340): remove this once destination paths are made relative to
        # the SDK root.
        else:
            parsed_id = urlparse(atom.identifier)
            base = parsed_id.netloc + parsed_id.path
            for file in atom.files:
                add(os.path.join(base, file.destination), file.source)
    add('meta/manifest.json', args.meta)

    with open(args.output, 'w') as output_file:
        for mapping in sorted(all_files.iteritems()):
            output_file.write('%s=%s\n' % mapping)


if __name__ == '__main__':
    sys.exit(main())
