#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(
        description=
        'Run ffx create-update and generate a depfile for the implicit inputs')
    parser.add_argument('--ffx-path', required=True)
    parser.add_argument('--depfile', type=argparse.FileType('w'), required=True)
    parser.add_argument(
        '--partitions', type=argparse.FileType('r'), required=True)
    parser.add_argument('--outdir', required=True)
    parser.add_argument('--gendir', required=True)
    parser.add_argument('--board-name', required=True)
    parser.add_argument('--epoch', required=True)
    parser.add_argument('--version-file', required=True)
    parser.add_argument('--packages')
    parser.add_argument('--update-package-name')
    parser.add_argument('--system-a', type=argparse.FileType('r'))
    parser.add_argument('--system-b', type=argparse.FileType('r'))
    parser.add_argument('--system-r', type=argparse.FileType('r'))
    args = parser.parse_args()

    # A list of the implicit deps to write to a depfile.
    deps = []

    # The command and arguments for ffx create-update.
    ffx_command = [
        args.ffx_path,
        "--config",
        "assembly_enabled=true",
        "assembly",
        "create-update",
        "--partitions",
        args.partitions.name,
        "--outdir",
        args.outdir,
        "--gendir",
        args.gendir,
        "--board-name",
        args.board_name,
        "--epoch",
        args.epoch,
        "--version-file",
        args.version_file,
    ]

    # Add the images to the deps list.
    def add_system_images_deps(manifest_file):
        if manifest_file:
            manifest = json.load(manifest_file)
            deps.extend([image['path'] for image in manifest])

    # Helper methods for passing a key:value argument pair to ffx.
    def add_argument(key, value):
        ffx_command.extend([key, value])

    def maybe_add_argument(key, value):
        if value:
            add_argument(key, value)

    def maybe_add_file_argument(key, file):
        if file:
            add_argument(key, file.name)

    # Add all the bootloaders as deps.
    deps.append(args.partitions.name)
    partitions = json.load(args.partitions)
    for bootloader in partitions["bootloader_partitions"]:
        deps.append(bootloader['image'])

    # Add all the system images as deps.
    add_system_images_deps(args.system_a)
    add_system_images_deps(args.system_b)
    add_system_images_deps(args.system_r)

    # Add the optional arguments.
    maybe_add_argument("--packages", args.packages)
    maybe_add_argument("--update-package-name", args.update_package_name)
    maybe_add_file_argument("--system-a", args.system_a)
    maybe_add_file_argument("--system-b", args.system_b)
    maybe_add_file_argument("--system-r", args.system_r)

    # Write the depfile.
    args.depfile.write('build.ninja: ' + ' '.join(deps))

    # Run ffx create-update.
    subprocess.run(ffx_command)


if __name__ == '__main__':
    sys.exit(main())
