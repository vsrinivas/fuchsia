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
    parser.add_argument(
        '--kernel-cmdline', type=argparse.FileType('r'), required=True)
    parser.add_argument(
        '--kernel-clock-backstop', type=argparse.FileType('r'), required=True)
    parser.add_argument(
        '--kernel-image-metadata', type=argparse.FileType('r'), required=True)
    parser.add_argument('--boot-args', type=argparse.FileType('r'))
    parser.add_argument(
        '--bootfs-entries', type=argparse.FileType('r'), required=True)
    parser.add_argument('--output', type=argparse.FileType('w'), required=True)
    args = parser.parse_args()

    # The config that will be created
    config = {}

    # Base package config
    if args.base_packages_list is not None:
        base_packages_list = json.load(args.base_packages_list)
        config["base"] = base_packages_list

    if args.cache_packages_list is not None:
        cache_packages_list = json.load(args.cache_packages_list)
        config["cache"] = cache_packages_list

    extra_packages = []
    if args.extra_files_packages_list is not None:
        extra_file_packages = json.load(args.extra_files_packages_list)
        extra_packages.extend(extra_file_packages)
    if args.extra_deps_files_packages_list is not None:
        extra_deps_file_packages = json.load(
            args.extra_deps_files_packages_list)
        for extra_dep in extra_deps_file_packages:
            extra_packages.append(extra_dep["package_manifest"])
    config["system"] = extra_packages

    # ZBI Config
    kernel_metadata = json.load(args.kernel_image_metadata)

    # The build_api_module("images") entry with name "kernel" and type "zbi"
    # is the kernel ZBI to include in the bootable ZBI.  There can be only one.
    [kernel_path] = [
        image["path"]
        for image in kernel_metadata
        if image["name"] == "kernel" and image["type"] == "zbi"
    ]
    config["kernel"] = {
        "path": kernel_path,
        "args": json.load(args.kernel_cmdline),
        "clock_backstop": json.load(args.kernel_clock_backstop),
    }

    if args.boot_args is not None:
        config["boot_args"] = json.load(args.boot_args)

    config["bootfs_files"] = json.load(args.bootfs_entries)

    json.dump(config, args.output, indent=2)


if __name__ == '__main__':
    sys.exit(main())
