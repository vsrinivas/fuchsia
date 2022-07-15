#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import hashlib
import json
import sys
from typing import Dict, Iterable, List, Set, TypeVar

from assembly import ImageAssemblyConfig, PackageManifest
from assembly import FileEntry, FilePath
from serialization import json_load

PackageName = str
FileHash = str
T = TypeVar('T')

# packages and bootfs filenames which we knowingly mutate in the product assembly process
MISMATCH_PACKAGE_EXCEPTIONS = [
    "config-data", "password_authenticator", "session_manager"
]
ADDITIONAL_PACKAGE_EXCEPTIONS = []
MISMATCH_FILE_EXCEPTIONS = ["meta/console.cvf"]


def compare_pkg_sets(
        first: Iterable[FilePath], second: Iterable[FilePath],
        setname: str) -> List[str]:
    # If the paths are the same, then we can just stop.
    if first == second:
        return []
    first_map = build_package_manifest_map(first)
    second_map = build_package_manifest_map(second)

    if first_map == second_map:
        return []

    return compare_packages(first_map, second_map, setname)


def build_package_manifest_map(
        package_paths: Iterable[FilePath]) -> Dict[FilePath, PackageManifest]:
    result: Dict[FilePath, PackageManifest] = {}
    paths_by_name: Dict[PackageName, FilePath] = {}

    for path in package_paths:
        with open(path, 'rb') as file:
            manifest = json_load(PackageManifest, file)
            package_name = manifest.package.name

            if package_name in paths_by_name:
                raise ValueError(
                    "Found a duplicate package name: {}  at: {} and: {}".format(
                        package_name, path, paths_by_name[package_name]))
            paths_by_name[package_name] = path

            result[path] = manifest

    return result


def compare_packages(
        first: Dict[FilePath, PackageManifest],
        second: Dict[FilePath, PackageManifest], setname: str) -> List[str]:
    errors: List[str] = []

    first_by_name = {
        manifest.package.name: (path, manifest)
        for path, manifest in first.items()
    }
    second_by_name = {
        manifest.package.name: (path, manifest)
        for path, manifest in second.items()
    }

    for missing in set(first_by_name.keys()).difference(second_by_name.keys()):
        errors.append(f"Missing package ({setname}): {missing}")

    for extra in set(second_by_name.keys()).difference(first_by_name.keys()):
        if extra not in ADDITIONAL_PACKAGE_EXCEPTIONS:
            errors.append(f"Extra package ({setname}): {extra}")

    for name in sorted(set(first_by_name.keys()).intersection(
            second_by_name.keys())):
        if name in MISMATCH_PACKAGE_EXCEPTIONS:
            continue

        first_path, first_manifest = first_by_name[name]
        second_path, second_manifest = second_by_name[name]

        pkg_compare_errors = first_manifest.compare_with(
            second_manifest, allow_source_path_differences=True)
        if pkg_compare_errors:
            errors.extend(
                [
                    f"package error ({setname}): {name} {error}"
                    for error in pkg_compare_errors
                ])

    return errors


def compare_file_entry_sets(
        first: Iterable[FileEntry], second: Iterable[FileEntry],
        setname: str) -> List[str]:
    first_map = build_file_entry_source_hash_map(first)
    second_map = build_file_entry_source_hash_map(second)
    if first_map == second_map:
        return []
    return compare_file_hash_maps(
        first_map, second_map, "{} file entry".format(setname))


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
        first_map: Dict[FilePath, FileHash],
        second_map: Dict[FilePath, FileHash], item_type: str) -> List[str]:
    errors: List[str] = []
    for (name, file_hash) in sorted(first_map.items()):
        if name in MISMATCH_FILE_EXCEPTIONS:
            continue

        if name not in second_map:
            errors.append(f"missing ({item_type}): {name}")
            continue

        # Use pop() so only items in second, but not in first, remain after
        # this loop.
        second_file_hash = second_map.pop(name)
        if file_hash != second_file_hash:
            errors.append(
                f"hash mismatch for {item_type} {name}: {file_hash} vs {second_file_hash}"
            )

    if second_map:
        for name in second_map.keys():
            if name in MISMATCH_FILE_EXCEPTIONS:
                continue
            errors.append(f"added ({item_type}): {name}")

    return errors


def compare_sets(item_type: str, first: Set[T], second: Set[T]) -> List[str]:
    errors: List[str] = []

    errors.extend(
        [f"added ({item_type}): {item}" for item in first.difference(second)])
    errors.extend(
        [f"missing ({item_type}): {item}" for item in second.difference(first)])

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(
        description=
        "Tool for comparing the contents of two different Image Assembly config files"
    )

    parser.add_argument(
        "--legacy-image-assembly-config",
        help='The legacy Image Assembly config',
        type=argparse.FileType('r'),
        required=True)
    parser.add_argument(
        "--generated-image-assembly-config",
        help='The Product Assembly-generated Image Assembly config',
        type=argparse.FileType('r'),
        required=True)
    parser.add_argument(
        "--product-assembly-config",
        help=
        "The Product Assembly configuration used to generate the Image Assembly config",
        type=argparse.FileType('r'),
        required=False)
    parser.add_argument(
        "--stamp",
        help='The file to touch when the action has been run sucessfully')

    args = parser.parse_args()

    legacy = ImageAssemblyConfig.json_load(args.legacy_image_assembly_config)
    generated = ImageAssemblyConfig.json_load(
        args.generated_image_assembly_config)
    product_assembly_config = {}
    if args.product_assembly_config:
        product_assembly_config = json.load(args.product_assembly_config)

    product_config = product_assembly_config.get('product', {})
    product_packages = product_config.get('packages', {})

    errors = []
    errors.extend(
        compare_pkg_sets(
            legacy.base | set(
                [
                    entry["manifest"]
                    for entry in product_packages.get('base', [])
                ]), generated.base, "base"))
    errors.extend(
        compare_pkg_sets(
            legacy.cache | set(
                [
                    entry["manifest"]
                    for entry in product_packages.get('cache', [])
                ]), generated.cache, "cache"))
    errors.extend(
        compare_pkg_sets(
            legacy.system | set(
                [
                    entry["manifest"]
                    for entry in product_packages.get('system', [])
                ]), generated.system, "system"))

    errors.extend(
        compare_file_entry_sets(
            legacy.bootfs_files, generated.bootfs_files, "bootfs"))

    errors.extend(
        compare_sets("boot arg", legacy.boot_args, generated.boot_args))

    if legacy.kernel.path and not generated.kernel.path:
        errors.append("generated image assembly config is missing a kernel")
    elif not legacy.kernel.path and generated.kernel.path:
        errors.append("legacy image assembly config is missing a kernel")
    elif legacy.kernel.path and generated.kernel.path:
        if hash_file(legacy.kernel.path) != hash_file(generated.kernel.path):
            errors.append(
                "kernels are different: {} and {}".format(
                    legacy.kernel.path, generated.kernel.path))

    errors.extend(
        compare_sets("kernel arg", legacy.kernel.args, generated.kernel.args))

    if legacy.kernel.clock_backstop != generated.kernel.clock_backstop:
        errors.append(
            "clock_backstops are different: {} and {}".format(
                legacy.kernel.clock_backstop, generated.kernel.clock_backstop))

    if errors:
        print('Errors found comparing image assembly configs:')
        print(
            f'      legacy image assembly config: {args.legacy_image_assembly_config.name}'
        )
        print(
            f'   generated image assembly config: {args.generated_image_assembly_config.name}'
        )
        if args.product_assembly_config:
            print(
                f'      from product assembly config: {args.product_assembly_config.name}'
            )
        for error in errors:
            print(error, file=sys.stderr)
    else:
        with open(args.stamp, 'w') as stamp_file:
            stamp_file.write('Match!\n')

    return 0 - len(errors)


if __name__ == "__main__":
    sys.exit(main())
