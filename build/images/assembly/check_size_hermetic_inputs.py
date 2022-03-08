#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def get_blob_paths(manifest_path):
    with open(manifest_path, 'rt') as file:
        manifest = json.load(file)
        return [blob["source_path"] for blob in manifest["blobs"]]


def main():
    parser = argparse.ArgumentParser(
        description=
        'Generates a file describing the dependencies of the size checker')
    parser.add_argument('--budgets', type=argparse.FileType('r'), required=True)
    parser.add_argument('--output', type=argparse.FileType('w'), required=True)
    parser.add_argument('--with-package-content', action='store_true')
    args = parser.parse_args()
    budgets = json.load(args.budgets)
    manifests = set(
        pkg for budget in budgets["package_set_budgets"]
        for pkg in budget["packages"])
    inputs = set(manifests)  # This copy the set copy.

    if args.with_package_content:
        for manifest in manifests:
            inputs.update(get_blob_paths(manifest))

    args.output.writelines(f"{input}\n" for input in sorted(inputs))


if __name__ == '__main__':
    sys.exit(main())
