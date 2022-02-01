#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import subprocess
import sys
from typing import Set

from depfile import DepFile
from assembly import AssemblyInputBundle


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
    meta_package_content = {'name': args.name, 'version': 0}
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
        # Add the AIB's package metafar to the creation manifest.
        creation_manifest.write("meta.far={}\n".format(args.meta_far))
        # Followed by all files from the AIB's contents manifest.
        for line in contents_manifest:
            # Split out the lines so that a depfile for the action can be made
            # from the contents_manifest's source paths.
            (dst, src) = line.split('=', 1)
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
    first = AssemblyInputBundle.load(args.first)
    second = AssemblyInputBundle.load(args.second)
    result = first.difference(second)
    if args.output:
        result.dump(args.output)
    else:
        print(result)


def intersect_bundles(args: argparse.Namespace) -> None:
    bundles = [AssemblyInputBundle.load(file) for file in args.bundles]
    result = bundles[0]
    for next_bundle in bundles[1:]:
        result = result.intersection(next_bundle)
    if args.output:
        result.dump(args.output)
    else:
        print(result)


def main():
    parser = argparse.ArgumentParser(
        description=
        "Tool for creating Assembly Input Bundles in-tree, for use with out-of-tree assembly"
    )
    sub_parsers = parser.add_subparsers(
        title="Commands",
        description="Commands for working with Assembly Input Bundles")

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

    archive_creation_parser = sub_parsers.add_parser(
        "generate-archive",
        help=
        "(build tool) Generate the tarmaker creation manifest for the tgz that contains an Assembly Input Bundle."
    )
    archive_creation_parser.add_argument("--tarmaker", required=True)
    archive_creation_parser.add_argument(
        "--contents-manifest", type=argparse.FileType('r'), required=True)
    archive_creation_parser.add_argument("--meta-far", required=True)
    archive_creation_parser.add_argument("--creation-manifest", required=True)
    archive_creation_parser.add_argument("--output", required=True)
    archive_creation_parser.add_argument(
        "--depfile", type=argparse.FileType('w'))
    archive_creation_parser.set_defaults(handler=generate_archive)

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
