#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys


def extract_name(label, default):
    ''' Extracts a reasonable name from the given GN label.

        Returns the given default value if no name could be extracted.
        '''
    # Label: //path/to/element:target
    (path, target) = label[2:].split(':')
    # Path: path/to/element
    parts = path.split('/')
    # Parts: [<layer>, public, lib, ...]
    if len(parts) <= 3 or parts[1] != 'public' or parts[2] != 'lib':
        return default
    if parts[-1] != target:
        parts.append(target)
    return '_'.join(parts[3:])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out',
                        help='Path to the output file',
                        required=True)
    parser.add_argument('--default-name',
                        help='Default name if an appropriate name could not be computed',
                        required=True)
    parser.add_argument('--label',
                        help='Label of the GN target',
                        required=True)
    args = parser.parse_args()

    with open(args.out, 'w') as out_file:
        out_file.write(extract_name(args.label, args.default_name))


if __name__ == '__main__':
    sys.exit(main())
