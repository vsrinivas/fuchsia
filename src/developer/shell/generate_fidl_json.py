#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Converts a distribution manifest to the Fuchsia INI format.'''

import argparse
import json


def main():
    parser = argparse.ArgumentParser(fromfile_prefix_chars='@')
    parser.add_argument(
        '--out-file', type=argparse.FileType('w'), required=True)
    parser.add_argument('fidl_json_files', nargs='+')
    args = parser.parse_args()

    output = []
    for fidl_json_file in args.fidl_json_files:
        with open(fidl_json_file, 'r') as f:
            fidl_json = json.load(f)
        output.append(
            {
                'source':
                    fidl_json_file,
                'destination':
                    'data/fidling/' + fidl_json["name"].replace('.', '/') +
                    ".fidl.json"
            })
    json.dump(output, args.out_file)


if __name__ == '__main__':
    main()
