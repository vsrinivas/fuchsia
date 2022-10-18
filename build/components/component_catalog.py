#!/usr/bin/env python3
#
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import csv
import glob
import json
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(
        "Produce a CSV report from GN component_catalog")
    parser.add_argument(
        "--csv", help="CSV output", type=argparse.FileType("w"), default="-")
    parser.add_argument(
        "--clean",
        help="Whether to run `fx clean`",
        action=argparse.BooleanOptionalAction,
        default=True,
    )
    parser.add_argument(
        "--gen",
        help="Whether to run `fx gen`",
        action=argparse.BooleanOptionalAction,
        default=True,
    )
    parser.add_argument(
        "--reference-manifest",
        help="Check for SDK atoms not in the given manifest",
        type=argparse.FileType("r"),
        required=False)
    parser.add_argument(
        "--build-dir", help="Ninja build directory, e.g. out/default")
    args = parser.parse_args()

    # fx clean
    # Needed to remove any stale metadata
    if args.clean:
        subprocess.check_call(
            ("fx", "clean"), stdout=sys.stdout, stderr=sys.stderr)

    # fx gen
    # Needed to generate metadata
    if args.gen:
        subprocess.check_call(
            ("fx", "gen"), stdout=sys.stdout, stderr=sys.stderr)

    if not args.reference_manifest:
        # Use //sdk/manifest/core.manifest by default
        args.reference_manifest = open(
            os.path.join(
                os.environ["FUCHSIA_DIR"], "sdk", "manifests", "core.manifest"))
    reference_sdk_ids = set(args.reference_manifest.readlines())

    # Harvest $FUCHSIA_BUILD_DIR/**/*.component_catalog.json
    if not args.build_dir:
        args.build_dir = subprocess.check_output(
            ("fx", "get-build-dir"), encoding="utf8").strip()
    glob_root = os.path.relpath(os.path.join(args.build_dir, "obj"))
    suffix = ".component_catalog.json"
    jsons = glob.glob("**/*" + suffix, root_dir=glob_root, recursive=True)

    # Compose into a single table
    rows = []
    for json_file in jsons:
        row = {
            # foo/bar/qux.component_catalog.json -> foo/bar:qux
            "label": ":".join(json_file[:-len(suffix)].rsplit("/", maxsplit=1)),
            "component_manifest_path": "",
            "has_cxx": False,
            "has_rust": False,
            "has_go": False,
            "has_dart": False,
            "has_driver": False,
            "sdk_ids": set(),
            "not_in_manifest": set(),
        }
        for clause in json.load(open(os.path.join(glob_root, json_file))):
            for key, value in clause.items():
                if key == "label":
                    # This is only useful for debugging
                    pass
                elif key == "sdk_id":
                    row["sdk_ids"].add(value)
                else:
                    row[key] = value
        row["not_in_manifest"] = row["sdk_ids"].difference(reference_sdk_ids)
        # Turn all sets to whitespace-separated strings
        row["sdk_ids"] = " ".join(sorted(row["sdk_ids"]))
        row["not_in_manifest"] = " ".join(sorted(row["not_in_manifest"]))
        rows.append(row)

    # Output as csv
    fieldnames = [
        "label",
        "component_manifest_path",
        "has_cxx",
        "has_rust",
        "has_go",
        "has_dart",
        "has_driver",
        "sdk_ids",
        "not_in_manifest",
    ]
    writer = csv.DictWriter(args.csv, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(sorted(rows, key=lambda row: row["label"]))


if __name__ == "__main__":
    sys.exit(main())
