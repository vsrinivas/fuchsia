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
    parser.add_argument(
        '--driver_path', required=True, help='The path to the driver.')
    parser.add_argument(
        '--doc_input',
        type=argparse.FileType('r'),
        help='The input JSON documentation file.')
    parser.add_argument(
        '--doc_output',
        required=True,
        type=argparse.FileType('w'),
        help='The output JSON documentation file.')

    args = parser.parse_args()

    documentation = {
        'short_description': '',
        'manufacturer': '',
        'families': '',
        'models': '',
        'areas': '',
        'path': args.driver_path
    }
    if (args.doc_input):
        doc_contents = json.load(args.doc_input)
        documentation['short_description'] = doc_contents['short_description']
        documentation['manufacturer'] = doc_contents['manufacturer']
        documentation['families'] = doc_contents['families']
        documentation['models'] = doc_contents['models']
        documentation['areas'] = doc_contents['areas']

    json.dump(documentation, args.doc_output)


if __name__ == "__main__":
    main()
