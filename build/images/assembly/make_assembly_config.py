#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser(
        description='Create the json configuration blob for image assembly')
    parser.add_argument('--base-packages-list', type=argparse.FileType('r'))
    parser.add_argument('--cache-packages-list', type=argparse.FileType('r'))
    parser.add_argument(
        '--extra-files-packages-list', type=argparse.FileType('r'))
    parser.add_argument(
        '--extra-deps-files-packages-list', type=argparse.FileType('r'))
    parser.add_argument('--version-file')
    parser.add_argument('--epoch-file')
    parser.add_argument(
        '--kernel-cmdline', type=argparse.FileType('r'), required=True)
    parser.add_argument(
        '--kernel-image-metadata', type=argparse.FileType('r'), required=True)
    parser.add_argument('--boot-args', type=argparse.FileType('r'))
    parser.add_argument(
        '--bootfs-entries', type=argparse.FileType('r'), required=True)
    parser.add_argument('--update-package-name', type=str, required=True)
    parser.add_argument('--base-package-name', type=str, required=True)
    parser.add_argument('--output', type=argparse.FileType('w'), required=True)
    args = parser.parse_args()

    # The config that will be created
    config = {}

    # Base package config
    if args.base_packages_list is not None:
        base_packages_list = json.load(args.base_packages_list)
        config["base_packages"] = base_packages_list

    if args.cache_packages_list is not None:
        cache_packages_list = json.load(args.cache_packages_list)
        config["cache_packages"] = cache_packages_list

    extra_packages = []
    if args.extra_files_packages_list is not None:
        extra_file_packages = json.load(args.extra_files_packages_list)
        extra_packages.extend(extra_file_packages)
    if args.extra_deps_files_packages_list is not None:
        extra_deps_file_packages = json.load(
            args.extra_deps_files_packages_list)
        for extra_dep in extra_deps_file_packages:
            extra_packages.append(extra_dep["package_manifest"])
    config["extra_packages_for_base_package"] = extra_packages

    if args.version_file is not None:
        config["version_file"] = args.version_file

    if args.epoch_file is not None:
        config["epoch_file"] = args.epoch_file

    # ZBI Config
    kernel_metadata = json.load(args.kernel_image_metadata)

    # The build_api_module("images") entry with name "kernel" and type "zbi"
    # is the kernel ZBI to include in the bootable ZBI.  There can be only one.
    [kernel_path] = [
        image["path"]
        for image in kernel_metadata
        if image["name"] == "kernel" and image["type"] == "zbi"
    ]
    config["kernel_image"] = kernel_path
    config["kernel_cmdline"] = json.load(args.kernel_cmdline)

    if args.boot_args is not None:
        config["boot_args"] = json.load(args.boot_args)

    config["bootfs_files"] = json.load(args.bootfs_entries)
    config["update_package_name"] = args.update_package_name
    config["base_package_name"] = args.base_package_name

    json.dump(config, args.output, indent=2)


if __name__ == '__main__':
    sys.exit(main())
