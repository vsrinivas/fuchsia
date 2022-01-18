#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Create a driver documentation from driver metadata."""

import json
import argparse
import os


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True, help='A name for the driver.')
    parser.add_argument(
        '--driver_path', required=True, help='The path to the driver.')
    parser.add_argument(
        '--doc_input', help='The input JSON documentation file.')
    parser.add_argument(
        '--doc_output',
        required=True,
        type=argparse.FileType('w'),
        help='The output JSON documentation file.')

    args = parser.parse_args()

    documentation = {
        'name': args.name,
        'short_description': '',
        'manufacturer': '',
        'families': [],
        'models': [],
        'areas': [],
        'path': args.driver_path,
        'supported_system_configurations': []
    }
    if (args.doc_input):
        with open(args.doc_input) as doc_input_file:
            doc_contents = json.load(doc_input_file)
            if len(doc_contents['short_description']) < 1:
                raise Exception(
                    "Driver info file: {}, must include a \"short_description\""
                    .format(args.doc_input))
            if len(doc_contents['short_description']) > 80:
                raise Exception(
                    "Driver info file: {} \"short_description\" must be less than 80 characters"
                    .format(args.doc_input))
            documentation['short_description'] = doc_contents[
                'short_description']
            documentation['manufacturer'] = doc_contents['manufacturer']
            documentation['families'] = doc_contents['families']
            documentation['models'] = doc_contents['models']
            if 'supported_system_configurations' in doc_contents.keys():
                documentation['supported_system_configurations'] = doc_contents[
                    'supported_system_configurations']
            if len(doc_contents['areas']) < 1:
                raise Exception(
                    "Driver info file: {}, must include at least one item in \"areas\""
                    .format(args.doc_input))
            documentation['areas'] = doc_contents['areas']

    json.dump(documentation, args.doc_output)


if __name__ == "__main__":
    main()
