#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Make the legacy configuration set

Create an ImageAssembly configuration based on the GN-generated config, after
removing any other configuration sets from it.
"""

import argparse
from collections import defaultdict
import json
import sys
from typing import Dict, List, Set, Tuple

from assembly import AssemblyInputBundle, AIBCreator, FileEntry, FilePath, ImageAssemblyConfig
from depfile import DepFile

# Some type annotations for clarity
PackageManifestList = List[FilePath]
Merkle = str
BlobList = List[Tuple[Merkle, FilePath]]
FileEntryList = List[FileEntry]
FileEntrySet = Set[FileEntry]
DepSet = Set[FilePath]


def copy_to_assembly_input_bundle(
    legacy: ImageAssemblyConfig, config_data_entries: FileEntryList,
    outdir: FilePath, base_driver_packages_list: List[str],
    base_driver_components_files_list: List[dict], shell_commands: Dict[str,
                                                                        List]
) -> Tuple[AssemblyInputBundle, FilePath, DepSet]:
    """
    Copy all the artifacts from the ImageAssemblyConfig into an AssemblyInputBundle that is in
    outdir, tracking all copy operations in a DepFile that is returned with the resultant bundle.

    Some notes on operation:
        - <outdir> is removed and recreated anew when called.
        - hardlinks are used for performance
        - the return value contains a list of all files read/written by the
        copying operation (ie. depfile contents)
    """
    aib_creator = AIBCreator(outdir)
    aib_creator.base = legacy.base
    aib_creator.cache = legacy.cache
    aib_creator.system = legacy.system
    aib_creator.bootfs_files = legacy.bootfs_files
    aib_creator.bootfs_packages = legacy.bootfs_packages
    aib_creator.kernel = legacy.kernel
    aib_creator.boot_args = legacy.boot_args

    aib_creator.base_drivers = set(base_driver_packages_list)
    if len(aib_creator.base_drivers) != len(base_driver_packages_list):
        raise ValueError(
            f"Duplicate package specified "
            " in base_driver_packages: {base_driver_packages_list}")
    aib_creator.base_driver_component_files = base_driver_components_files_list
    aib_creator.config_data = config_data_entries
    aib_creator.shell_commands = shell_commands
    return aib_creator.build()


def main():
    parser = argparse.ArgumentParser(
        description=
        "Create an image assembly configuration that is what remains after removing the configs to 'subtract'"
    )
    parser.add_argument(
        "--image-assembly-config", type=argparse.FileType('r'), required=True)
    parser.add_argument("--config-data-entries", type=argparse.FileType('r'))
    parser.add_argument(
        "--subtract", default=[], nargs="*", type=argparse.FileType('r'))
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--depfile", type=argparse.FileType('w'))
    parser.add_argument("--export-manifest", type=argparse.FileType('w'))
    parser.add_argument(
        "--base-driver-packages-list", type=argparse.FileType('r'))
    parser.add_argument(
        "--base-driver-components-files-list", type=argparse.FileType('r'))
    parser.add_argument(
        "--shell-commands-packages-list", type=argparse.FileType('r'))
    args = parser.parse_args()

    # Read in the legacy config and the others to subtract from it
    legacy: ImageAssemblyConfig = ImageAssemblyConfig.json_load(
        args.image_assembly_config)
    subtract = [ImageAssemblyConfig.json_load(other) for other in args.subtract]

    # Subtract each from the legacy config, in the order given in args.
    for other in subtract:
        legacy = legacy.difference(other)

    # Read in the config_data entries if available.
    if args.config_data_entries:
        config_data_entries = [
            FileEntry.from_dict(entry)
            for entry in json.load(args.config_data_entries)
        ]
    else:
        config_data_entries = []

    base_driver_packages_list = None
    if args.base_driver_packages_list:
        base_driver_packages_list = json.load(args.base_driver_packages_list)
        base_driver_components_files_list = json.load(
            args.base_driver_components_files_list)

    shell_commands = dict()
    shell_deps = set()
    if args.shell_commands_packages_list:
        shell_commands = defaultdict(set)
        for package in json.load(args.shell_commands_packages_list):
            manifest_path, package_name = package["manifest_path"], package[
                "package_name"]
            with open(manifest_path, "r") as fname:
                package_aib = json.load(fname)
                shell_deps.add(manifest_path)
                shell_commands[package_name].update(
                    {
                        blob["path"]
                        for blob in package_aib["blobs"]
                        if blob['path'].startswith("bin/")
                    })

    # Create an Assembly Input Bundle from the remaining contents
    (assembly_input_bundle, assembly_config_manifest_path,
     deps) = copy_to_assembly_input_bundle(
         legacy, config_data_entries, args.outdir, base_driver_packages_list,
         base_driver_components_files_list, {
             package: sorted(list(components))
             for (package, components) in shell_commands.items()
         })

    deps.update(shell_deps)
    # Write out a fini manifest of the files that have been copied, to create a
    # package or archive that contains all of the files in the bundle.
    if args.export_manifest:
        assembly_input_bundle.write_fini_manifest(
            args.export_manifest, base_dir=args.outdir)

    # Write out a depfile.
    if args.depfile:
        dep_file = DepFile(assembly_config_manifest_path)
        dep_file.update(deps)
        dep_file.write_to(args.depfile)


if __name__ == "__main__":
    sys.exit(main())
