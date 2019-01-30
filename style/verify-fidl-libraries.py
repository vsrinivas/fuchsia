#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import subprocess
import sys


FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # style
    os.path.abspath(__file__))))

NAMESPACES = [
    'fidl',
    'fuchsia',
    'test',
]


def main():
    parser = argparse.ArgumentParser(
            description=('Checks that FIDL libraries in a given layer are '
                         'properly namespaced'))
    layer_group = parser.add_mutually_exclusive_group(required=True)
    layer_group.add_argument('--layer',
                             help='Name of the layer to analyze',
                             choices=['zircon', 'garnet', 'peridot', 'topaz'])
    layer_group.add_argument('--vendor-layer',
                             help='Name of the vendor layer to analyze')
    parser.add_argument('--namespaces',
                        help='The list of allowed namespaces, defaults to '
                             '[%s]' % ', '.join(NAMESPACES),
                        nargs='*',
                        default=NAMESPACES)
    args = parser.parse_args()

    if args.layer:
        base = os.path.join(FUCHSIA_ROOT, args.layer)
    else:
        base = os.path.join(FUCHSIA_ROOT, 'vendor', args.vendor_layer)

    files = subprocess.check_output(['git', '-C', base, 'ls-files', '*.fidl'])

    has_errors = False
    for file in files.splitlines():
        with open(os.path.join(base, file), 'r') as fidl:
            contents = fidl.read()
            result = re.search(r'^library ([^\.;]+)[^;]*;$', contents,
                               re.MULTILINE)
            if not result:
                print('Missing library declaration (%s)' % file)
                has_errors = True
                continue
            namespace = result.group(1)
            if namespace not in args.namespaces:
                print(
                    'Invalid namespace %s (%s), namespace must begin with one of [%s].'
                    % (namespace, file, ', '.join(NAMESPACES)))
                has_errors = True

    return 1 if has_errors else 0


if __name__ == '__main__':
    sys.exit(main())
