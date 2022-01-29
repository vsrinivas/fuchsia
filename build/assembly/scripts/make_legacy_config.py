#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Make the legacy configuration set

Create an ImageAssembly configuration based on the GN-generated config, after
removing any other configuration sets from it.
"""

import argparse
import json
import os
import pathlib
import shutil
import sys
from typing import Any, Dict, Iterable, List, Set, Tuple

from assembly import fast_copy, AssemblyInputBundle, ConfigDataEntries, ImageAssemblyConfig
from assembly import FileEntry, FilePath
from depfile import DepFile

# Some type annotations for clarity
PackageManifestList = List[FilePath]
Merkle = str
BlobList = List[Tuple[Merkle, FilePath]]
FileEntryList = List[FileEntry]
FileEntrySet = Set[FileEntry]
DepSet = Set[FilePath]


def copy_to_assembly_input_bundle(
        config: ImageAssemblyConfig, config_data_entries: FileEntryList,
        outdir: FilePath) -> Tuple[AssemblyInputBundle, DepSet]:
    """
    Copy all the artifacts from the ImageAssemblyConfig into an AssemblyInputBundle that is in
    outdir, tracking all copy operations in a DepFile that is returned with the resultant bundle.

    Some notes on operation:
        - <outdir> is removed and recreated anew when called.
        - hardlinks are used for performance
        - the return value contains a list of all files read/written by the
        copying operation (ie. depfile contents)
    """
    # Remove the existing <outdir>, and recreate it
    if os.path.exists(outdir):
        shutil.rmtree(outdir)
    os.makedirs(outdir)

    # Track all files we read
    deps: DepSet = set()

    # Init an empty resultant AssemblyInputBundle
    result = AssemblyInputBundle()

    # Copy over the boot args and zbi kernel args, unchanged, into the resultant
    # assembly bundle
    result.boot_args = config.boot_args
    kernel_args = config.kernel.args
    if kernel_args:
        result.kernel.args = kernel_args
    kernel_backstop = config.kernel.clock_backstop
    if kernel_backstop:
        result.kernel.clock_backstop = kernel_backstop

    # Copy the manifests for the base package set into the assembly bundle
    (base_pkgs, base_blobs, base_deps) = copy_packages(config, outdir, "base")
    deps.update(base_deps)
    result.base.update(base_pkgs)

    # Strip any base pkgs from the cache set
    config.cache = config.cache.difference(config.base)

    # Copy the manifests for the cache package set into the assembly bundle
    (cache_pkgs, cache_blobs,
     cache_deps) = copy_packages(config, outdir, "cache")
    deps.update(cache_deps)
    result.cache.update(cache_pkgs)

    # Copy the manifests for the system package set into the assembly bundle
    (system_pkgs, system_blobs,
     system_deps) = copy_packages(config, outdir, "system")
    deps.update(system_deps)
    result.system.update(system_pkgs)

    # Deduplicate all blobs by merkle, but don't validate unique sources for
    # each merkle, last one wins (we trust that in the in-tree build isn't going
    # to make invalid merkles)
    all_blobs = {}
    for (merkle, source) in [*base_blobs, *cache_blobs, *system_blobs]:
        all_blobs[merkle] = source

    # Copy all the blobs to their dir in the out-of-tree layout
    (all_blobs, blob_deps) = copy_blobs(all_blobs, outdir)
    deps.update(blob_deps)
    result.blobs = set(
        [os.path.relpath(blob_path, outdir) for blob_path in all_blobs])

    # Copy the bootfs entries
    (bootfs,
     bootfs_deps) = copy_file_entries(config.bootfs_files, outdir, "bootfs")
    deps.update(bootfs_deps)
    result.bootfs_files.update(bootfs)

    # Rebase the path to the kernel into the out-of-tree layout
    kernel_src_path: Any = config.kernel.path
    kernel_filename = os.path.basename(kernel_src_path)
    kernel_dst_path = os.path.join("kernel", kernel_filename)
    result.kernel.path = kernel_dst_path

    # Copy the kernel itself into the out-of-tree layout
    local_kernel_dst_path = os.path.join(outdir, kernel_dst_path)
    os.makedirs(os.path.dirname(local_kernel_dst_path), exist_ok=True)
    fast_copy(kernel_src_path, local_kernel_dst_path)
    deps.add(kernel_src_path)

    # Copy the config_data entries into the out-of-tree layout
    (config_data,
     config_data_deps) = copy_config_data_entries(config_data_entries, outdir)
    deps.update(config_data_deps)
    result.config_data = config_data

    return (result, deps)


def copy_packages(
        config: ImageAssemblyConfig, outdir: FilePath,
        set_name: str) -> Tuple[PackageManifestList, BlobList, DepSet]:
    """Copy package manifests to the assembly bundle outdir, returning the set of blobs
    that need to be copied as well (so that they blob copying can be done in a
    single, deduplicated step.)
    """
    package_manifests = getattr(config, set_name)

    # Resultant paths to package manifests
    packages = []

    # All of the blobs to copy, deduplicated by merkle, and validated for
    # conflicting sources.
    blobs: BlobList = []

    # The deps touched by this function.
    deps: DepSet = set()

    # Bail early if empty
    if len(package_manifests) == 0:
        return (packages, blobs, deps)

    # Create the directory for the packages, now that we know it will exist
    packages_dir = os.path.join("packages", set_name)
    os.makedirs(os.path.join(outdir, packages_dir), exist_ok=True)

    # Open each manifest, record the blobs, and then copy it to its destination,
    # sorted by path to the package manifest.
    for package_manifest_path in sorted(package_manifests):
        with open(package_manifest_path, 'r') as file:
            manifest = json.load(file)
            package_name = manifest["package"]["name"]
            # Track in deps, since it was opened.
            deps.add(package_manifest_path)

        # But skip config-data, if we find it.
        if "config-data" == package_name:
            continue

        # Add the blobs to the set of all blobs, validating that duplicates
        # don't have conflicting sources.
        for blob_entry in manifest["blobs"]:
            source = blob_entry["source_path"]
            merkle = blob_entry["merkle"]
            blob = (merkle, source)
            blobs.append(blob)

        # Copy the manifest to its destination.
        rebased_destination = os.path.join(packages_dir, package_name)
        package_manifest_destination = os.path.join(outdir, rebased_destination)
        fast_copy(package_manifest_path, package_manifest_destination)

        # Track the package manifest in our set of packages
        packages.append(rebased_destination)

    return (packages, blobs, deps)


def copy_blobs(blobs: Dict[Merkle, FilePath],
               outdir: FilePath) -> Tuple[List[FilePath], DepSet]:
    blob_paths: List[FilePath] = []
    deps: DepSet = set()

    # Bail early if empty
    if len(blobs) == 0:
        return (blob_paths, deps)

    # Create the directory for the blobs, now that we know it will exist.
    blobs_dir = os.path.join(outdir, "blobs")
    os.makedirs(blobs_dir)

    # Copy all blobs
    for (merkle, source) in blobs.items():
        blob_destination = os.path.join(blobs_dir, merkle)
        blob_paths.append(blob_destination)
        fast_copy(source, blob_destination)
        deps.add(source)

    return (blob_paths, deps)


def copy_file_entries(entries: FileEntrySet, outdir: FilePath,
                      set_name: str) -> Tuple[FileEntryList, DepSet]:
    results: FileEntryList = []
    deps: DepSet = set()

    # Bail early if nothing to do
    if len(entries) == 0:
        return (results, deps)

    for entry in entries:
        rebased_destination = os.path.join(set_name, entry.destination)
        copy_destination = os.path.join(outdir, rebased_destination)

        # Create parents if they don't exist
        os.makedirs(os.path.dirname(copy_destination), exist_ok=True)

        # Hardlink the file from source to the destination, relative to the
        # directory for all entries.
        fast_copy(entry.source, copy_destination)

        # Make a new FileEntry, which has a source of the path within the
        # out-of-tree layout, and the same destination.
        results.append(
            FileEntry(
                source_path=rebased_destination, dest_path=entry.destination))

        # Add the copied file's source/destination to the set of touched files.
        deps.add(entry.source)

    return (results, deps)


def copy_config_data_entries(
        entries: FileEntryList,
        outdir: FilePath) -> Tuple[ConfigDataEntries, DepSet]:
    """
    Take a list of entries for the config_data package, copy them into the
    appropriate layout for the assembly input bundle, and then return the
    config data entries and the set of DepEntries from the copying

    This expects the entries to be destined for:
    `meta/data/<package>/<path/to/file>`

    and creates a ConfigDataEntries dict of PackageName:FileEntryList.
    """
    results: ConfigDataEntries = {}
    deps: DepSet = set()

    if len(entries) == 0:
        return (results, deps)

    # Make a sorted list of a deduplicated set of the entries.
    for entry in sorted(set(entries)):
        # Crack the in-package path apart
        #
        # "meta" / "data" / package_name / path/to/file
        parts = pathlib.Path(entry.destination).parts
        if parts[:2] != ("meta", "data"):
            raise ValueError(
                "Found an unexpected destination path: {}".format(parts))
        package_name = parts[2]
        file_path = os.path.join(*parts[3:])

        rebased_source_path = os.path.join(
            "config_data", package_name, file_path)
        copy_destination = os.path.join(outdir, rebased_source_path)

        # Make any needed parents directories
        os.makedirs(os.path.dirname(copy_destination), exist_ok=True)

        # Hardlink the file from source to the destination
        fast_copy(entry.source, copy_destination)

        # Append the entry to the set of entries for the package
        results.setdefault(package_name, set()).add(
            FileEntry(dest_path=file_path, source_path=rebased_source_path))

        # Add the copy operation to the depfile
        deps.add(entry.source)

    return (results, deps)


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
    args = parser.parse_args()

    # Read in the legacy config and the others to subtract from it
    legacy: ImageAssemblyConfig = ImageAssemblyConfig.load(
        args.image_assembly_config)
    subtract = [ImageAssemblyConfig.load(other) for other in args.subtract]

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

    assembly_config_manifest_path = os.path.join(
        args.outdir, "assembly_config.json")

    dep_file = DepFile(assembly_config_manifest_path)

    # Copy the remaining contents to the out-of-tree Assembly Input Bundle layout
    (assembly_input_bundle, deps) = copy_to_assembly_input_bundle(
        legacy, config_data_entries, args.outdir)
    dep_file.update(deps)

    # Write out the resultant config into the outdir of the out-of-tree layout.
    # the copy_set paths are all relative to cwd, so use the full cwd to the
    # file as the dest_path, which will be rebased when generating the fini
    # manifest later.
    with open(assembly_config_manifest_path, 'w') as outfile:
        assembly_input_bundle.write_to(outfile)

    # Write out a fini manifest of the files that have been copied, to create a
    # package or archive that contains all of the files in the bundle.
    if args.export_manifest:
        assembly_input_bundle.write_fini_manifest(
            args.export_manifest, base_dir=args.outdir)

    # Write out a depfile.
    if args.depfile:
        dep_file.write_to(args.depfile)


if __name__ == "__main__":
    sys.exit(main())
