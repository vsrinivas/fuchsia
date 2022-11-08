#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import subprocess
import sys
from typing import Dict, List, Set, Tuple
import logging
from depfile import DepFile
from assembly import AssemblyInputBundle, AIBCreator, DriverDetails, FilePath, PackageManifest
from serialization.serialization import json_load
logger = logging.getLogger()


def create_bundle(args: argparse.Namespace) -> None:
    """Create an Assembly Input Bundle (AIB).
    """
    aib_creator = AIBCreator(args.outdir)

    # Add the base and cache packages, if they exist.
    if args.base_pkg_list:
        add_pkg_list_from_file(aib_creator, args.base_pkg_list, "base")

    if args.cache_pkg_list:
        add_pkg_list_from_file(aib_creator, args.cache_pkg_list, "cache")

    if args.bootfs_pkg_list:
        add_pkg_list_from_file(
            aib_creator, args.bootfs_pkg_list, "bootfs_packages")

    if args.shell_cmds_list:
        add_shell_commands_from_file(aib_creator, args.shell_cmds_list)

    if args.drivers_pkg_list:
        add_driver_list_from_file(aib_creator, args.drivers_pkg_list)

    # Add any bootloaders.
    if args.qemu_kernel:
        aib_creator.qemu_kernel = args.qemu_kernel

    # Create the AIB itself.
    (assembly_input_bundle, assembly_config, deps) = aib_creator.build()

    # Write out a dep file if one is requested.
    if args.depfile:
        DepFile.from_deps(assembly_config, deps).write_to(args.depfile)

    # Write out a fini manifest of the files that have been copied, to create a
    # package or archive that contains all of the files in the bundle.
    if args.export_manifest:
        assembly_input_bundle.write_fini_manifest(
            args.export_manifest, base_dir=args.outdir)


def add_pkg_list_from_file(
        aib_creator: AIBCreator, pkg_list_file, pkg_set_name: str):
    pkg_set: Set = getattr(aib_creator, pkg_set_name)
    pkg_list = _read_json_file(pkg_list_file)
    for pkg_manifest_path in pkg_list:
        if pkg_manifest_path in pkg_set:
            raise ValueError(
                f"duplicate pkg manifest found: {pkg_manifest_path}")
        pkg_set.add(pkg_manifest_path)


def add_driver_list_from_file(aib_creator: AIBCreator, driver_list_file):
    pkg_set: Set = getattr(aib_creator, "base")
    driver_details_list = _read_json_file(driver_list_file)
    for driver_details in driver_details_list:
        if driver_details["package_target"] in pkg_set:
            raise ValueError(
                f"duplicate pkg manifest found: {driver_details['package_target']}"
            )
        aib_creator.provided_base_driver_details.append(
            DriverDetails(
                driver_details["package_target"],
                driver_details["driver_components"]))


def add_shell_commands_from_file(
        aib_creator: AIBCreator, shell_commands_list_file):
    """
    [
        {
            "components": [
                "ls"
            ],
            "package": "ls"
        }
    ]
    """
    loaded_file = _read_json_file(shell_commands_list_file)

    for command in loaded_file:
        package = command["package"]
        components = command["components"]
        aib_creator.shell_commands[package].extend(components)


def _read_json_file(pkg_list_file):
    try:
        return json.load(pkg_list_file)
    except:
        logger.exception(f"While parsing {pkg_list_file.name}")
        raise


def generate_package_creation_manifest(args: argparse.Namespace) -> None:
    """Generate a package creation manifest for an Assembly Input Bundle (AIB)

    Each AIB has a contents manifest that was created with it.  This file lists
    all of the files in the AIB, and their path within the build dir::

      AIB/path/to/file_1=outdir/path/to/AIB/path/to/file_1
      AIB/path/to/file_2=outdir/path/to/AIB/path/to/file_2

    This format locates all the files in the AIB, relative to the
    root_build_dir, and then gives their destination path within the AIB package
    and archive.

    To generate the package the AIB, a creation manifest is required (also in
    FINI format).  This is the same file, with the addition of the path to the
    package metadata file::

      meta/package=path/to/metadata/file

    This fn generates the package metadata file, and then generates the creation
    manifest file by appending the path to the metadata file to the entries in
    the AIB contents manifest.
    """
    meta_package_content = {'name': args.name, 'version': '0'}
    json.dump(meta_package_content, args.meta_package)
    contents_manifest = args.contents_manifest.read()
    args.output.write(contents_manifest)
    args.output.write("meta/package={}".format(args.meta_package.name))


def generate_archive(args: argparse.Namespace) -> None:
    """Generate an archive of an Assembly Input Bundle (AIB)

    Each AIB has a contents manifest that was created with it.  This file lists
    all of the files in the AIB, and their path within the build dir::

      AIB/path/to/file_1=outdir/path/to/AIB/path/to/file_1
      AIB/path/to/file_2=outdir/path/to/AIB/path/to/file_2

    This format locates all the files in the AIB, relative to the
    root_build_dir, and then gives their destination path within the AIB package
    and archive.

    To generate the archive of the AIB, a creation manifest is required (also in
    FINI format).  This is the same file, with the addition of the path to the
    package meta.far.

      meta.far=path/to/meta.far

    This fn generates the creation manifest, appending the package meta.far to
    the contents manifest, and then calling the tarmaker tool to build the
    archive itself, using the generated creation manifest.
    """
    deps: Set[str] = set()
    # Read the AIB's contents manifest, all of these files will be added to the
    # creation manifest for the archive.
    contents_manifest = args.contents_manifest.readlines()
    deps.add(args.contents_manifest.name)
    with open(args.creation_manifest, 'w') as creation_manifest:

        if args.meta_far:
            # Add the AIB's package meta.far to the creation manifest if one was
            # provided.
            creation_manifest.write("meta.far={}\n".format(args.meta_far))

        # Add all files from the AIB's contents manifest.
        for line in contents_manifest:
            # Split out the lines so that a depfile for the action can be made
            # from the contents_manifest's source paths.
            src = line.split('=', 1)[1]
            deps.add(src.strip())
            creation_manifest.write(line)

    # Build the archive itself.
    cmd_args = [
        args.tarmaker, "-manifest", args.creation_manifest, "-output",
        args.output
    ]
    subprocess.run(cmd_args, check=True)

    if args.depfile:
        DepFile.from_deps(args.output, deps).write_to(args.depfile)


def diff_bundles(args: argparse.Namespace) -> None:
    first = AssemblyInputBundle.json_load(args.first)
    second = AssemblyInputBundle.json_load(args.second)
    result = first.difference(second)
    if args.output:
        result.json_dump(args.output)
    else:
        print(result)


def intersect_bundles(args: argparse.Namespace) -> None:
    bundles = [AssemblyInputBundle.json_load(file) for file in args.bundles]
    result = bundles[0]
    for next_bundle in bundles[1:]:
        result = result.intersection(next_bundle)
    if args.output:
        result.json_dump(args.output)
    else:
        print(result)


def find_blob(args: argparse.Namespace) -> None:
    bundle = AssemblyInputBundle.json_load(args.bundle_config)
    bundle_dir = os.path.dirname(args.bundle_config.name)
    found_at: List[Tuple[FilePath, FilePath]] = []
    for pkg_set in [bundle.base, bundle.cache, bundle.system]:
        for pkg_manifest_path in pkg_set:
            with open(os.path.join(bundle_dir, pkg_manifest_path),
                      'r') as pkg_manifest_file:
                manifest = json_load(PackageManifest, pkg_manifest_file)
                for blob in manifest.blobs:
                    if blob.merkle == args.blob:
                        found_at.append((pkg_manifest_path, blob.path))
    if found_at:
        pkg_header = "Package"
        path_header = "Path"
        pkg_column_width = max(
            len(pkg_header), *[len(entry[0]) for entry in found_at])
        path_column_width = max(
            len(path_header), *[len(entry[1]) for entry in found_at])
        formatter = f"{{0: <{pkg_column_width}}}  | {{1: <{path_column_width}}}"
        header = formatter.format(pkg_header, path_header)
        print(header)
        print("=" * len(header))
        for pkg, path in found_at:
            print(formatter.format(pkg, path))


def main():
    parser = argparse.ArgumentParser(
        description=
        "Tool for creating Assembly Input Bundles in-tree, for use with out-of-tree assembly"
    )
    sub_parsers = parser.add_subparsers(
        title="Commands",
        description="Commands for working with Assembly Input Bundles")

    ###
    #
    # 'assembly_input_bundle_tool create' subcommand parser
    #
    bundle_creation_parser = sub_parsers.add_parser(
        "create", help="Create an Assembly Input Bundle")
    bundle_creation_parser.add_argument(
        "--outdir",
        required=True,
        help="Path to the outdir that will contain the AIB")
    bundle_creation_parser.add_argument(
        "--base-pkg-list",
        type=argparse.FileType('r'),
        help=
        "Path to a json list of package manifests for the 'base' package set")
    bundle_creation_parser.add_argument(
        "--bootfs-pkg-list",
        type=argparse.FileType('r'),
        help=
        "Path to a json list of package manifests for the 'bootfs' package set")
    bundle_creation_parser.add_argument(
        "--drivers-pkg-list",
        type=argparse.FileType('r'),
        help="Path to a json list of driver details for the 'base' package set")
    bundle_creation_parser.add_argument(
        "--shell-cmds-list",
        type=argparse.FileType('r'),
        help=
        "Path to a json list of dictionaries with the manifest path as key and a list of shell_command components as the value"
    )
    bundle_creation_parser.add_argument(
        "--cache-pkg-list",
        type=argparse.FileType('r'),
        help=
        "Path to a json list of package manifests for the 'cache' package set")
    bundle_creation_parser.add_argument(
        "--qemu-kernel", help="Path to the qemu kernel")
    bundle_creation_parser.add_argument(
        "--depfile",
        type=argparse.FileType('w'),
        help="Path to write a dependency file to")
    bundle_creation_parser.add_argument(
        "--export-manifest",
        type=argparse.FileType('w'),
        help="Path to write a FINI manifest of the contents of the AIB")
    bundle_creation_parser.set_defaults(handler=create_bundle)

    ###
    #
    # 'assembly_input_bundle_tool diff' subcommand parser
    #
    diff_bundles_parser = sub_parsers.add_parser(
        "diff",
        help=
        "Calculate the difference between the first and second bundles (A-B).")
    diff_bundles_parser.add_argument(
        "first", help='The first bundle (A)', type=argparse.FileType('r'))
    diff_bundles_parser.add_argument(
        "second", help='The second bundle (B)', type=argparse.FileType('r'))
    diff_bundles_parser.add_argument(
        "--output",
        help='A file to write the output to, instead of stdout.',
        type=argparse.FileType('w'))
    diff_bundles_parser.set_defaults(handler=diff_bundles)

    ###
    #
    # 'assembly_input_bundle_tool intersect' subcommand parser
    #
    intersect_bundles_parser = sub_parsers.add_parser(
        "intersect", help="Calculate the intersection of the provided bundles.")
    intersect_bundles_parser.add_argument(
        "bundles",
        nargs="+",
        action="extend",
        help='Paths to the bundle configs.',
        type=argparse.FileType('r'))
    intersect_bundles_parser.add_argument(
        "--output",
        help='A file to write the output to, instead of stdout.',
        type=argparse.FileType('w'))
    intersect_bundles_parser.set_defaults(handler=intersect_bundles)

    ###
    #
    # 'assembly_input_bundle_tool generate-package-creation-manifest' subcommand
    #  parser
    #
    package_creation_manifest_parser = sub_parsers.add_parser(
        "generate-package-creation-manifest",
        help=
        "(build tool) Generate the creation manifest for the package that contains an Assembly Input Bundle."
    )
    package_creation_manifest_parser.add_argument(
        "--contents-manifest", type=argparse.FileType('r'), required=True)
    package_creation_manifest_parser.add_argument("--name", required=True)
    package_creation_manifest_parser.add_argument(
        "--meta-package", type=argparse.FileType('w'), required=True)
    package_creation_manifest_parser.add_argument(
        "--output", type=argparse.FileType('w'), required=True)
    package_creation_manifest_parser.set_defaults(
        handler=generate_package_creation_manifest)

    ###
    #
    # 'assembly_input_bundle_tool generate-archive' subcommand parser
    #
    archive_creation_parser = sub_parsers.add_parser(
        "generate-archive",
        help=
        "(build tool) Generate the tarmaker creation manifest for the tgz that contains an Assembly Input Bundle."
    )
    archive_creation_parser.add_argument("--tarmaker", required=True)
    archive_creation_parser.add_argument(
        "--contents-manifest", type=argparse.FileType('r'), required=True)
    archive_creation_parser.add_argument("--meta-far")
    archive_creation_parser.add_argument("--creation-manifest", required=True)
    archive_creation_parser.add_argument("--output", required=True)
    archive_creation_parser.add_argument(
        "--depfile", type=argparse.FileType('w'))
    archive_creation_parser.set_defaults(handler=generate_archive)

    ###
    #
    # 'assembly_input_bundle_tool find-blob' subcommand parser
    #
    find_blob_parser = sub_parsers.add_parser(
        "find-blob",
        help=
        "Find what causes a blob to be included in the Assembly Input Bundle.")
    find_blob_parser.add_argument(
        "--bundle-config",
        required=True,
        type=argparse.FileType('r'),
        help="Path to the assembly_config.json for the bundle")
    find_blob_parser.add_argument(
        "--blob", required=True, help="Merkle of the blob to search for.")
    find_blob_parser.set_defaults(handler=find_blob)

    args = parser.parse_args()

    if "handler" in args:
        # Dispatch to the handler fn.
        args.handler(args)
    else:
        # argparse doesn't seem to automatically catch that not subparser was
        # called, and so if there isn't a handler function (which is set by
        # having specified a subcommand), then just display usage instead of
        # a cryptic KeyError.
        parser.print_help()


if __name__ == "__main__":
    sys.exit(main())
