#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Combine multiple driver manifests into a single driver manifest."""
import json
import argparse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--manifest-file-list', type=argparse.FileType('r'), required=True)
    parser.add_argument('--output', type=argparse.FileType('w'), required=True)
    parser.add_argument(
        '--dep-file', type=argparse.FileType('w'), required=True)
    args = parser.parse_args()

    combined_manifest = []
    manifest_files = args.manifest_file_list.read().splitlines()
    for manifest_file in manifest_files:
        with open(manifest_file) as f2:
            new_manifest = json.load(f2)
            for driver in new_manifest:
                combined_manifest.append(driver)

    json.dump(combined_manifest, args.output)

    args.dep_file.write(
        '{}: {}\n'.format(args.output.name, ' '.join(manifest_files)))


if __name__ == "__main__":
    main()
