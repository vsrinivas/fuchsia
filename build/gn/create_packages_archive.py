#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Build packages.archive.tgz."""

import argparse
import filecmp
import json
import os
import pathlib
import subprocess
import sys

from assembly import PackageManifest
from serialization import json_load
from typing import Dict, Set


def add_content_entry(
        content: Dict[str, str], dst: str, src: str, src_dir: str,
        depfile_inputs: Set[str]):
    """Add new entry to |content|, checking for duplicates."""
    # Make the source path relative, because the build still
    # puts absolute one in a small number of package manifests :-/
    src_path = os.path.relpath(os.path.join(src_dir, src))
    depfile_inputs.add(src_path)
    if dst in content:
        cur_src_path = content[dst]
        if cur_src_path != src_path and not filecmp.cmp(cur_src_path, src_path):
            raise ValueError(
                'Duplicate entries for destination %s:\n  %s\n  %s\n' %
                (dst, cur_src_path, src_path))
        return
    content[dst] = src_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        '--files',
        metavar='FILESPEC',
        nargs='*',
        default=[],
        help=
        'Add one or more file to the archive, each FILESPEC should be DST=SRC')

    parser.add_argument(
        '--src-dir',
        default='',
        help='Specify source directory. Default is current one.')

    parser.add_argument(
        '--tuf-repo-name', required=True, help='Specify TUF repository name.')

    parser.add_argument(
        '--tarmaker',
        required=True,
        help='Path to the \'tarmaker\' host tool to use')

    parser.add_argument(
        '--tarmaker-manifest',
        required=True,
        help='Path to output tarmaker manifest file.')

    parser.add_argument(
        '--package-manifests-list',
        required=True,
        help='Path to file listing all package manifest files.')

    parser.add_argument(
        '--tuf-timestamp-json', required=True, help='Path to timestamp.json')

    parser.add_argument(
        '--tuf-targets-json',
        nargs='*',
        default=[],
        help='Path to targets.json file')

    parser.add_argument(
        '--output', required=True, help='Path to output .tar.gz archive.')

    parser.add_argument('--depfile', help='Path to output Ninja depfile.')

    args = parser.parse_args()

    # Map destination path -> source path
    content: Dict[str, str] = {}

    # Map merkle root string -> source path
    merkle_to_source: Dict[str, str] = {}

    # List of input paths for the depfile.
    depfile_inputs: Set[str] = set()

    # Parse the --files argument first.
    for filespec in args.files:
        dst, sep, src = filespec.strip().partition('=')
        if sep != '=':
            raise ValueError('Unexpected file spec: ' + filespec)
        add_content_entry(content, dst, src, args.src_dir, depfile_inputs)

    # Parse --package-manifests-list if provided, parse it to
    # populate the blobs directory in the archive.
    blobs_dst_dir = args.tuf_repo_name + '/repository/blobs'
    with open(args.package_manifests_list) as f:
        package_manifest_paths = f.read().splitlines()

    for package_manifest_path in package_manifest_paths:
        depfile_inputs.add(package_manifest_path)
        with open(package_manifest_path) as f:
            package_manifest = json_load(PackageManifest, f)
        # TODO(https://fxbug.dev/98482) extract this into a constructor for PackageManifest?
        for blob_entry in package_manifest.blobs:
            src = blob_entry.source_path
            if package_manifest.blob_sources_relative == "file":
                src = pathlib.Path(package_manifest_path).parent / src
            merkle = blob_entry.merkle
            merkle_to_source[merkle] = src
            dst = os.path.join(blobs_dst_dir, merkle)
            add_content_entry(content, dst, src, args.src_dir, depfile_inputs)

    # Parse the timestamp.json file to extract the version of the
    # snapshot.json file to use, copy it to the archive, then parse it.
    depfile_inputs.add(args.tuf_timestamp_json)
    with open(args.tuf_timestamp_json) as f:
        timestamp_json = json.load(f)

    tuf_repo_dir = os.path.join(args.src_dir, args.tuf_repo_name, 'repository')
    add_content_entry(
        content, os.path.join(tuf_repo_dir, 'timestamp.json'),
        args.tuf_timestamp_json, '', depfile_inputs)

    # Install <version>.snapshot.json as is, then as 'snapshot.json'.
    snapshot_json_version = timestamp_json['signed']['meta']['snapshot.json'][
        'version']
    snapshot_json_file = os.path.join(
        tuf_repo_dir, '%d.snapshot.json' % snapshot_json_version)
    snapshot_json_src_path = os.path.join(args.src_dir, snapshot_json_file)
    add_content_entry(
        content, snapshot_json_file, snapshot_json_src_path, '', depfile_inputs)
    add_content_entry(
        content, os.path.join(tuf_repo_dir, 'snapshot.json'),
        snapshot_json_src_path, '', depfile_inputs)

    # Parse the snapshots.json file to grab the version targets.json.
    # Install <version>.snapshot.json as-is, then as snapshot.json in the
    # archive.
    #
    with open(snapshot_json_src_path) as f:
        snapshot_json = json.load(f)

    targets_json_version = snapshot_json['signed']['meta']['targets.json'][
        'version']
    targets_json_file = os.path.join(
        tuf_repo_dir, '%d.targets.json' % targets_json_version)
    targets_json_src_path = os.path.join(args.src_dir, targets_json_file)
    add_content_entry(
        content, targets_json_file, targets_json_src_path, '', depfile_inputs)
    add_content_entry(
        content, os.path.join(tuf_repo_dir, 'targets.json'), targets_json_file,
        '', depfile_inputs)

    # Parse the targets.json file to populate the targets directory
    # in the archive.
    with open(targets_json_src_path) as f:
        targets_json = json.load(f)
    for target, entry in targets_json['signed']['targets'].items():
        hash = entry['hashes']['sha512']
        merkle = entry['custom']['merkle']
        # The target name is <dirname>/<basename> or just <basename>.
        # While the installation file name is
        # ${tuf_repo_name}/repository/targets/<dirname>/<hash>.<basename>
        # or ${tuf_repo_name}/repository/targets/<hash>.<basename> is there
        # is no <dirname> in the target name,
        dirname, basename = os.path.split(target)
        dst = args.tuf_repo_name + '/repository/targets'
        if dirname:
            dst = os.path.join(dst, dirname)
        dst = os.path.join(dst, f'{hash}.{basename}')
        src = merkle_to_source[merkle]
        add_content_entry(content, dst, src, args.src_dir, depfile_inputs)

    # Verify that all content source files are available, or print an error
    # message describing what's missing.
    missing_files = []
    for dst, src in content.items():
        if not os.path.exists(src):
            missing_files.append(src)

    if missing_files:
        print(
            'ERROR: Source files required to build the archive are missing!:\n',
            file=sys.stderr)
        for path in missing_files[:16]:
            print('  ' + path, file=sys.stderr)
        if len(missing_files > 16):
            print('  ...', file=sys.stderr)
        return 1

    # Generate a tarmaker manifest file listing how to construct the archive,
    # then call the tool to build it.
    with open(args.tarmaker_manifest, 'w') as tarmaker_manifest:
        for dst, src in sorted(content.items()):
            tarmaker_manifest.write('%s=%s\n' % (dst, src))

    cmd = [
        args.tarmaker, '-output', args.output, '-manifest',
        args.tarmaker_manifest
    ]
    subprocess.run(cmd)

    # Generate the depfile if needed
    if args.depfile:
        with open(args.depfile, 'w') as f:
            f.write(args.output + ':')
            for input in sorted(depfile_inputs):
                f.write(' ' + input)
            f.write('\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
