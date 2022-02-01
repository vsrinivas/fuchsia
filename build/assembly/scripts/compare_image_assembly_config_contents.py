#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import hashlib
import json
from struct import pack
import sys
from typing import Dict, Iterable, List, Set, TypeVar, Union

from assembly import FileEntry, FilePath, ImageAssemblyConfig

PackageName = str
FileHash = str
T = TypeVar('T')


def compare_pkg_sets(
        setname: str, first: Iterable[FilePath],
        second: Iterable[FilePath]) -> List[str]:
    # If the paths are the same, then we can just stop.
    if first == second:
        return []
    first_map = build_package_name_contents_map(first)
    second_map = build_package_name_contents_map(second)
    if first_map == second_map:
        return []
    return compare_file_hash_maps(
        first_map, second_map, "{} package".format(setname))


def compare_file_entry_sets(
        setname: str, first: Iterable[FileEntry],
        second: Iterable[FileEntry]) -> List[str]:
    first_map = build_file_entry_source_hash_map(first)
    second_map = build_file_entry_source_hash_map(second)
    if first_map == second_map:
        return []
    return compare_file_hash_maps(
        first_map, second_map, "{} file entry".format(setname))


def build_package_name_contents_map(
        package_paths: Iterable[FilePath]) -> Dict[PackageName, FileHash]:
    result: Dict[PackageName, FileHash] = {}
    paths_by_name: Dict[PackageName, FilePath] = {}

    for path in package_paths:
        with open(path, 'rb') as file:
            manifest = json.load(file)
            package_name = manifest["package"]["name"]

            if package_name in paths_by_name:
                raise ValueError(
                    "Found a duplicate package name: {}  at: {} and: {}".format(
                        package_name, path, paths_by_name[package_name]))
            paths_by_name[package_name] = path

            merkle = get_package_merkle_from_manifest_json(manifest)
            if merkle is not None:
                result[package_name] = merkle
            else:
                raise ValueError(
                    "Could not find a meta.far entry for package manifest: {}".
                    format(path))

    return result


def get_package_merkle_from_manifest_json(manifest: Dict) -> Union[str, None]:
    for blob_entry in manifest["blobs"]:
        if blob_entry["path"] == "meta/":
            return blob_entry["merkle"]
    # Not found, return 'None'
    return None


def build_file_entry_source_hash_map(
        file_entries: Iterable[FileEntry]) -> Dict[FilePath, FileHash]:
    result: Dict[FilePath, FileHash] = {}

    for entry in file_entries:
        result[entry.destination] = hash_file(entry.source)

    return result


def hash_file(path: FilePath) -> FileHash:
    with open(path, 'rb') as raw_file:
        file_hash = hashlib.sha256(raw_file.read())
        return file_hash.hexdigest()


def compare_file_hash_maps(
        first_map: Dict[Union[PackageName, FilePath], FileHash],
        second_map: Dict[Union[PackageName, FilePath],
                         FileHash], item_type: str) -> List[str]:
    errors: List[str] = []
    for (name, file_hash) in sorted(first_map.items()):
        if name not in second_map:
            errors.append("{} {} missing from second".format(item_type, name))
            continue

        # Use pop() so only items in second, but not in first, remain after
        # this loop.
        second_file_hash = second_map.pop(name)
        if file_hash != second_file_hash:
            errors.append(
                "{} hash mismatch for {}: {} vs {}".format(
                    item_type, name, file_hash, second_file_hash))

    if second_map:
        for name in second_map.keys():
            errors.append("{} {} missing from first".format(item_type, name))

    return errors


def compare_sets(item_type: str, first: Set[T], second: Set[T]) -> List[str]:
    errors: List[str] = []

    errors.extend(
        [
            "{} {} missing from first".format(item_type, item)
            for item in first.difference(second)
        ])
    errors.extend(
        [
            "{} {} missing from second".format(item_type, item)
            for item in second.difference(first)
        ])

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(
        description=
        "Tool for comparing the contents of two different Image Assembly config files"
    )

    parser.add_argument(
        "first",
        help='The first Image Assembly config',
        type=argparse.FileType('r'))
    parser.add_argument(
        "second",
        help='The second Image Assembly config',
        type=argparse.FileType('r'))
    parser.add_argument(
        "--stamp",
        help='The file to touch when the action has been run sucessfully')

    args = parser.parse_args()

    first = ImageAssemblyConfig.load(args.first)
    second = ImageAssemblyConfig.load(args.second)

    errors = []
    errors.extend(compare_pkg_sets("base", first.base, second.base))
    errors.extend(compare_pkg_sets("cache", first.cache, second.cache))
    errors.extend(compare_pkg_sets("system", first.system, second.system))

    errors.extend(
        compare_file_entry_sets(
            "bootfs", first.bootfs_files, second.bootfs_files))

    errors.extend(compare_sets("boot arg", first.boot_args, second.boot_args))

    if hash_file(first.kernel.path) != hash_file(second.kernel.path):
        errors.append(
            "kernels are different: {} and {}".format(
                first.kernel.path, second.kernel.path))

    errors.extend(
        compare_sets("kernel arg", first.kernel.args, second.kernel.args))

    if first.kernel.clock_backstop != second.kernel.clock_backstop:
        errors.append(
            "clock_backstops are different: {} and {}".format(
                first.kernel.clock_backstop, second.kernel.clock_backstop))

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
    else:
        with open(args.stamp, 'w') as stamp_file:
            stamp_file.write('Match!\n')

    return 0 - len(errors)


if __name__ == "__main__":
    sys.exit(main())
