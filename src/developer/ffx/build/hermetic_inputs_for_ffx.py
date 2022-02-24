#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys
from typing import List


def main():
    parser = argparse.ArgumentParser(
        description=
        'Generate a hermetic inputs file for ffx_action by reading the sdk'
        'manifest and generating a list of all the files that are mentioned')
    parser.add_argument(
        '--sdk-manifest',
        type=argparse.FileType('r'),
        required=True,
        help='The path to the SDK manifest')
    parser.add_argument(
        '--additional-hermetic-inputs',
        type=argparse.FileType('r'),
        help='The path to another hermetic inputs file to merge in')
    parser.add_argument(
        '--output',
        type=argparse.FileType('w'),
        required=True,
        help='The path to the hermetic inputs file to write')
    args = parser.parse_args()

    inputs = []
    sdk_manifest = json.load(args.sdk_manifest)
    sdk_atoms = sdk_manifest.get('atoms', [])
    for atom in sdk_atoms:
        files = atom.get('files', [])
        files = [entry['source'] for entry in files]
        inputs.extend(files)
    if args.additional_hermetic_inputs:
        additional_inputs = args.additional_hermetic_inputs.readlines()
        additional_inputs = [i.strip() for i in additional_inputs]
        inputs.extend(additional_inputs)
    for input in inputs:
        args.output.write(input + '\n')


if __name__ == '__main__':
    sys.exit(main())
