#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate the Bazel legacy build inputs workspace content.

This script is used to populate the content of a Bazel workspace used to
expose Ninja-generated output files as Bazel inputs. The workspace will
be populated with symlinks, and will contain a single top-level
BUILD.bazel file that defines `filegroup()` targets.

Each --inputs argument should use the `<name>=<manifest>` format,
where `<name>` is the final Bazel `filegroup()` target name that
will appear in the generated workspace, and `<manifest>` points
to a file listing the Ninja build outputs for it.

The manifest is a JSON file that contains an array of objects
whose schema is described in //build/bazel/bazel_inputs.gni.
"""

import argparse
import errno
import json
import os
import shutil
import sys


class FileGroup(object):

    def __init__(self, entry):
        self._name = entry['name']
        self._entries = []
        self._is_dir = False
        if 'destinations' in entry:
            # A regular entry that list explicit sources and destinations.
            for dest, source in zip(entry['destinations'], entry['sources']):
                self._entries.append((dest, source))
        elif 'dest_dir' in entry:
            # A directory entry
            self._is_dir = True
            self._entries.append((entry['dest_dir'], entry['source_dir']))

    @property
    def name(self):
        return self._name

    @property
    def is_dir(self):
        return self._is_dir

    @property
    def entries(self):
        return self._entries


def force_symlink(src_path, dst_path):
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    link_path = os.path.relpath(src_path, os.path.dirname(dst_path))

    try:
        os.symlink(link_path, dst_path)
    except OSError as e:
        if e.errno == errno.EEXIST:
            os.remove(dst_path)
            os.symlink(link_path, dst_path)
        else:
            raise e


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--workspace-dir",
        help="Output Bazel workspace directory.",
        required=True)
    parser.add_argument(
        "--repository-name",
        help="Repository name for this workspace",
        required=True)
    parser.add_argument(
        "--source-dir",
        help="Specify alternative source directory. Default to the current one.",
        default=".",
    )
    parser.add_argument(
        "--input-manifest",
        required=True,
        help="A bazel inputs manifest specification file",
    )
    args = parser.parse_args()

    # Read all file group entries from the manifest.
    filegroups = []
    with open(args.input_manifest) as f:
        input_json = json.load(f)

    filegroups = [FileGroup(entry) for entry in input_json]

    out_dir = args.workspace_dir
    src_dir = args.source_dir

    # Clear content of output directory.
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    def write_output_file(name, content):
        with open(os.path.join(out_dir, name), "w") as f:
            f.write(content)

    bazel_build_content = r'''# Auto-generated - DO NOT EDIT!

package(default_visibility = ["//visibility:public"])

exports_files(
    glob(["**/*"])
)

'''
    for filegroup in filegroups:
        # Mirror all source files to their destination in the workspace directory.
        if filegroup.is_dir:
            dest_dir, source_dir = filegroup.entries[0]
            dst_path = os.path.abspath(os.path.join(out_dir, dest_dir))
            src_path = os.path.abspath(os.path.join(src_dir, source_dir))
            force_symlink(src_path, dst_path)
        else:
            for entry in filegroup.entries:
                dst_path = os.path.abspath(os.path.join(out_dir, entry[0]))
                src_path = os.path.abspath(os.path.join(src_dir, entry[1]))
                force_symlink(src_path, dst_path)

        # Add a named filegroup() target to the BUILD.bazel file content
        # that list all destination paths for this group.
        bazel_build_content += 'filegroup(\n    name = "{}",\n'.format(
            filegroup.name)
        if filegroup.is_dir:
            dest_dir, source_dir = filegroup.entries[0]
            if dest_dir[-1] != '/':
                dest_dir += '/'
            bazel_build_content += '    srcs = glob([\n'
            bazel_build_content += '        "{path}**",\n'.format(path=dest_dir)
            bazel_build_content += '    ]),\n'
        else:
            bazel_build_content += '    srcs = [\n'
            for entry in filegroup.entries:
                bazel_build_content += '        "{path}",\n'.format(
                    path=entry[0])

            bazel_build_content += '    ],\n'
        bazel_build_content += ')\n\n'

    # NOTE: The WORKSPACE.bazel is required, but its content is ignored by
    # Bazel, it is up to out/default/bazel/workspace/MODULE.bazel to properly
    # name the workspace.
    write_output_file("WORKSPACE.bazel", "")
    write_output_file("BUILD.bazel", bazel_build_content)
    write_output_file(
        "MODULE.bazel",
        'module(name = "%s", version = "1")\n' % args.repository_name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
