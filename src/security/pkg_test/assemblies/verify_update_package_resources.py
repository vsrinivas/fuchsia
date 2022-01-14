#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Verify that all the blobs designated in `--update-package-manifest-path` (an
# update package manifest) are also designated in `--package-manifest-path` (a
# package manifest). It is not an error for `--package-manifest-path` to
# designate additional blobs.

import argparse
import json
import sys


def verify_update_package_resources(package_manifest, update_package_manifest):
    if package_manifest["version"] != "1":
        raise ValueError(
            'Unsupported package_manifest.json schema version: %s' %
            package_manifest["version"])
    if update_package_manifest["version"] != "1":
        raise ValueError(
            'Unsupported update_package_manifest.json schema version: %s' %
            update_package_manifest["version"])
    package_merkles = set(
        [blob["merkle"] for blob in package_manifest["blobs"]])
    update_package_merkles = set(
        [blob["merkle"] for blob in update_package_manifest["blobs"]])
    if not update_package_merkles.issubset(package_merkles):
        missing_merkles = update_package_merkles.difference(package_merkles)
        missing_blobs = [
            blob for blob in update_package_manifest["blobs"]
            if blob["merkle"] in missing_merkles
        ]
        raise Exception(
            'Resource package is missing update package blob(s): %s' %
            json.dumps(missing_blobs, indent=4))


def main():
    parser = argparse.ArgumentParser(
        description=
        'Verify that update package blobs are a subset of other package blobs.')
    parser.add_argument(
        '--package-manifest-path',
        required=True,
        type=argparse.FileType('r'),
        help='Input package_manifest.json file')
    parser.add_argument(
        '--update-package-manifest-path',
        required=True,
        type=argparse.FileType('r'),
        help='Input update_package_manifest.json file')
    parser.add_argument(
        '--output',
        required=True,
        type=argparse.FileType('w'),
        help='Empty output file written only when verification is successful')
    args = parser.parse_args()
    package_manifest = json.load(args.package_manifest_path)
    update_package_manifest = json.load(args.update_package_manifest_path)
    verify_update_package_resources(package_manifest, update_package_manifest)
    args.output.write('')


if __name__ == "__main__":
    main()
