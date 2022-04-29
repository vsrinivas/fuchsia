#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys
from typing import List

from assembly import FileEntry, FilePath, PackageManifest
from serialization import json_load


def get_blob_path(relative_path: str, relative_to_file: str) -> str:
    file_parent = os.path.dirname(relative_to_file)
    path = os.path.join(file_parent, relative_path)
    path = os.path.realpath(path)
    path = os.path.relpath(path, os.getcwd())
    return path


def files_from_package_set(package_set: List[FilePath]) -> List[FilePath]:
    paths = []
    for manifest in package_set:
        paths.append(manifest)
        with open(manifest, 'r') as file:
            package_manifest = json_load(PackageManifest, file)
            blob_sources = []
            for blob in package_manifest.blobs:
                path = blob.source_path
                if package_manifest.blob_sources_relative:
                    path = get_blob_path(path, manifest)
                blob_sources.append(path)
            paths.extend(blob_sources)
    return paths


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
        inputs.update(files_from_package_set(manifests))

    args.output.writelines(f"{input}\n" for input in sorted(inputs))


if __name__ == '__main__':
    sys.exit(main())
