#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Converts a .json file listing fini manifests into a .system.rsp file.'''

import argparse
import json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--input', help='Path to input .json list', required=True)
    parser.add_argument(
        '--output', help='Path to the formatted manifest', required=True)
    parser.add_argument('--depfile', help='Path to the output depfile')
    args = parser.parse_args()

    with open(args.input, 'r') as input_file:
        fini_manifests = json.load(input_file)

    output = ''
    depfile = args.output + ':'
    for entry in fini_manifests:
        output += "--entry-manifest=%s\n" % entry['label']
        fini_manifest = entry['fini_manifest']
        depfile += ' %s' % fini_manifest
        with open(fini_manifest, 'r') as f:
            for line in f.readlines():
                if line.startswith('meta/'):
                    continue
                output += '--entry=' + line

    with open(args.output, 'w') as f:
        f.write(output)

    if args.depfile:
        with open(args.depfile, 'w') as f:
            f.write(depfile)


if __name__ == '__main__':
    main()
