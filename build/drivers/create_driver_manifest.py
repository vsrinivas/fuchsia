#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Create a Driver Manifest from a list of driver paths"""

import json
import argparse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--package-url',
        help='The package url to rebase the distribution_manifest paths to.')
    parser.add_argument(
        '--distribution_manifest_file',
        help=
        'Path to a distribution_manifest file which is a JSON file that contains driver paths.'
    )
    parser.add_argument(
        '--output',
        help='The path where this script will output the driver manifest.')
    args = parser.parse_args()

    with open(args.distribution_manifest_file) as f:
        distribution_manifest = json.load(f)

    manifest = [
        {
            'driver_url': args.package_url + '#' + entry['destination']
        }
        for entry in distribution_manifest
        if entry.get('destination', '').startswith('meta/') and
        entry.get("destination", '').endswith(".cm")
    ]

    json_manifest = json.dumps(manifest)

    with open(args.output, "w") as f:
        f.write(json_manifest)


if __name__ == "__main__":
    main()
