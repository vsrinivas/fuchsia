#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Create a global documentation file with driver provided data."""

import argparse
import os
import json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--doc_list',
        type=argparse.FileType('r'),
        required=True,
        help='Path to the list of drivers documentation files.')
    parser.add_argument(
        '--output',
        type=argparse.FileType('w'),
        required=True,
        help='The path for the output global documentation file.')
    parser.add_argument(
        '--dep_file', type=argparse.FileType('w'), required=True)

    args = parser.parse_args()

    doc_list = args.doc_list.read().splitlines()
    output_name = os.path.realpath(args.output.name)

    all_drivers_doc_dict = {'drivers_documentation': []}
    for doc_path in doc_list:
        with open(doc_path, "r") as f:
            json_text = f.read()
            json_dict = json.loads(json_text)
            all_drivers_doc_dict['drivers_documentation'].append(json_dict)

    json.dump(all_drivers_doc_dict, args.output)
    args.dep_file.write('{}: {}\n'.format(args.output.name, ' '.join(doc_list)))


if __name__ == "__main__":
    main()
