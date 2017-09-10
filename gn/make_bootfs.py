#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import paths
import platform
import re
import subprocess
import sys

from read_build_ids import read_build_id


def parse_build_ids_from_manifest(manifest, buildids):
    with open(manifest) as manifest_contents:
        for line in manifest_contents:
            equal_sign = line.find('=')
            if equal_sign == -1:
                continue
            path = line[equal_sign + 1:].strip()
            buildid = read_build_id(path)
            if buildid:
                # 'path' will be the path to the stripped binary e.g.:
                #   /foo/out/debug-x86-64/happy_bunny_test
                # We want the path to the unstripped binary which is in:
                #   /foo/out/debug-x86-64/exe.unstripped/happy_bunny_test
                # or
                #   /foo/out/debug-x86-64/lib.unstripped/happy_bunny_test
                # if it exists and matches the buildid.
                path_dir = os.path.dirname(path)
                path_base = os.path.basename(path)
                unstripped_locations = ['exe.unstripped', 'lib.unstripped']
                for location in unstripped_locations:
                    unstripped_path = os.path.join(path_dir,
                                                   location,
                                                   path_base)
                    unstripped_buildid = read_build_id(unstripped_path)
                    if unstripped_buildid == buildid:
                        path = unstripped_path
                        break
                buildids.append('%s %s\n' % (buildid, path))

def add_paths_to_map(path_map, manifest_path):
  with open(manifest_path) as file:
    for l in file:
      parts = l.split("=")
      if len(parts) != 2:
        continue
      if parts[0] in path_map:
        path_map[parts[0]].append(manifest_path)
      else:
        path_map[parts[0]] = [manifest_path]

def main():
    parser = argparse.ArgumentParser(
        description='Make a bootfs for loading into Magenta')
    parser.add_argument('--output-file', help='Place to put built userfs')
    parser.add_argument(
        '--build-id-map', help='Place to put mapping from build id to paths')
    parser.add_argument('--boot-manifest', help='Location of manifest for /boot')
    parser.add_argument('--system-manifest', help='Location of manifest for /system')
    parser.add_argument('--pre-binaries', help='bootdata binaries to include before bootfs')
    parser.add_argument('--post-binaries', help='bootdata binaries to include after bootfs')
    parser.add_argument('packages', nargs=argparse.REMAINDER)
    args = parser.parse_args()

    buildids = []
    out_targs = {}

    for manifest in [args.boot_manifest, args.system_manifest]:
        if os.path.exists(manifest):
            parse_build_ids_from_manifest(manifest, buildids)

    mkbootfs_cmd = [paths.MKBOOTFS_PATH, '-c']
    mkbootfs_cmd += ['-o', args.output_file]
    if args.pre_binaries:
        mkbootfs_cmd += [args.pre_binaries]
    if os.path.exists(args.boot_manifest) and os.path.getsize(args.boot_manifest) > 0:
        mkbootfs_cmd += ["--target=boot", args.boot_manifest]
        add_paths_to_map(out_targs, args.boot_manifest)
    if os.path.exists(args.system_manifest) and os.path.getsize(args.system_manifest) > 0:
        mkbootfs_cmd += ["--target=system", args.system_manifest]
        add_paths_to_map(out_targs, args.system_manifest)
    if args.post_binaries:
        mkbootfs_cmd += [args.post_binaries]
    for package in args.packages:
        package_dir = os.path.join("package", package)
        boot_manifest = os.path.join(package_dir, "boot_manifest")
        if os.path.exists(boot_manifest) and os.path.getsize(boot_manifest) != 0:
            mkbootfs_cmd += [ "--target=boot", boot_manifest ]
            add_paths_to_map(out_targs, boot_manifest)
        system_manifest = os.path.join(package_dir, "system_manifest")
        if os.path.exists(system_manifest) and os.path.getsize(system_manifest) != 0:
            mkbootfs_cmd += [ "--target=system", system_manifest ]
            add_paths_to_map(out_targs, system_manifest)
        ids = os.path.join(package_dir, "ids.txt")
        if os.path.exists(ids):
            with open(ids) as ids_file:
                buildids.append(ids_file.read())

    error = False
    for key, value in out_targs.items():
      if len(value) > 1:
        error = True
        sys.stderr.write("%s is an output target of multiple manifests: \"%s\""
                         % (key, value[0]))
        for path in value[1:]:
          sys.stderr.write(", \"%s\""  % path)
        sys.stderr.write("\n")

    if error:
      return -1

    with open(args.build_id_map, 'w') as build_id_file:
        build_id_file.writelines(buildids)

    return subprocess.call(mkbootfs_cmd)

if __name__ == '__main__':
    sys.exit(main())
