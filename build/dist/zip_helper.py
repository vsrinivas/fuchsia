#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Extracts a zip file and writes a partial distribution manifest.
# See //docs/development/build/concepts/build_system/internals/manifest_formats.md

from argparse import ArgumentParser
import json
from os import path
from zipfile import ZipFile

parser = ArgumentParser()
parser.add_argument(
    'zip',
    help='extracts a zip file and writes a partial distribution manifest')
parser.add_argument(
    '--output-dir', help='directory to which to extract', required=True)
parser.add_argument(
    '--output-manifest',
    help='path to which to output the distribution manifest',
    required=True)
parser.add_argument(
    '--output-depfile', help='path to the generated depfile', required=True)
parser.add_argument(
    '--dest-path-prefix',
    help='path to prefix all entries within the zip when writing the manifest')


def main():
    args = parser.parse_args()
    with ZipFile(args.zip) as archive:
        archive.extractall(args.output_dir)
        json_data = []
        depfile_data = []
        for entry in archive.infolist():
            destination = path.join(
                args.dest_path_prefix,
                entry.filename) if args.dest_path_prefix else entry.filename
            source = path.join(args.output_dir, entry.filename)
            json_data.append({'destination': destination, 'source': source})
            depfile_data.append(source)
        with open(args.output_manifest, 'w') as f:
            json.dump(json_data, f)
        with open(args.output_depfile, 'w') as f:
            f.write(' '.join(depfile_data))
            f.write(f': {args.zip}\n')


if __name__ == '__main__':
    main()
