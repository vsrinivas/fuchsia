#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate Bazel workspace + output_base for the Fuchsia platform build."""
import argparse
import errno
import os
import shutil
import stat
import sys


def get_host_platform():
    if sys.platform == 'linux':
        host_platform = 'linux'
    elif sys.platform == 'darwin':
        host_platform = 'mac'
    else:
        host_platform = os.uname().sysname
    return host_platform


def get_host_arch():
    host_arch = os.uname().machine
    if host_arch == 'x86_64':
        host_arch = 'x64'
    elif host_arch.startswith(('armv8', 'aarch64')):
        host_arch = 'arm64'
    return host_arch


def get_host_tag():
    return '%s-%s' % (get_host_platform(), get_host_arch())


def force_symlink(src_path, dst_path):
    link_path = os.path.relpath(src_path, os.path.dirname(dst_path))
    try:
        os.symlink(link_path, dst_path)
    except OSError as e:
        if e.errno == errno.EEXIST:
            os.remove(dst_path)
            os.symlink(link_path, dst_path)
        else:
            raise


def make_removeable(path):
    '''Ensure the file at |path| is removeable.'''
    info = os.stat(path, follow_symlinks=False)
    if info.st_mode & stat.S_IWUSR == 0:
        os.chmod(path, info.st_mode | stat.S_IWUSR, follow_symlinks=False)


def remove_dir(path):
    # shutil.rmtree() does not work well when there are readonly symlinks to
    # directories. This results in weird NotADirectory error when trying to
    # call os.scandir() or os.rmdir() on them (which happens internally).
    #
    # Re-implement it correctly here. This is not as secure as it could
    # (see "shutil.rmtree symlink attack"), but is sufficient for the Fuchsia
    # build.
    all_files = []
    all_dirs = []
    for root, subdirs, files in os.walk(path):
        # subdirs may contain actual symlinks which should be treated as
        # files here.
        real_subdirs = []
        for subdir in subdirs:
            if os.path.islink(os.path.join(root, subdir)):
                files.append(subdir)
            else:
                real_subdirs.append(subdir)

        for file in files:
            file_path = os.path.join(root, file)
            all_files.append(file_path)
            make_removeable(file_path)
        for subdir in real_subdirs:
            dir_path = os.path.join(root, subdir)
            all_dirs.append(dir_path)
            make_removeable(dir_path)

    for file in reversed(all_files):
        os.remove(file)
    for dir in reversed(all_dirs):
        os.rmdir(dir)
    os.rmdir(path)


def create_dir(path):
    if os.path.exists(path):
        remove_dir(path)
    os.makedirs(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fuchsia-dir', help='Path to the Fuchsia source tree')
    parser.add_argument('topdir', help='Top-level output directory')
    parser.add_argument(
        '--output_base_dir',
        help='Output base directory, defaults to $TOPDIR/output_base')
    parser.add_argument(
        '--workspace_dir',
        help='Output workspace directory, defaults to $TOPDIR/workspace')
    parser.add_argument(
        '--bazel-bin',
        help=
        'Path to bazel binary, defaults to $FUCHSIA_DIR/prebuilt/third_party/bazel/${host_platform}/bazel'
    )
    parser.add_argument(
        '--use-bzlmod',
        action='store_true',
        help='Use BzlMod to generate external repositories.')

    args = parser.parse_args()

    if not args.fuchsia_dir:
        # Assume this script is in 'build/bazel/scripts/'
        # //build/bazel:generate_main_workspace always sets this argument,
        # this feature is a convenience when calling this script manually
        # during platform build development and debugging.
        args.fuchsia_dir = os.path.abspath(
            os.path.join(os.path.dirname(__file__), '..', '..', '..'))

    host_tag = get_host_tag()

    ninja_binary = os.path.join(
        args.fuchsia_dir, 'prebuilt', 'third_party', 'ninja', host_tag, 'ninja')

    if not args.output_base_dir:
        args.output_base_dir = os.path.join(args.topdir, 'output_base')
    if not args.workspace_dir:
        args.workspace_dir = os.path.join(args.topdir, 'workspace')

    if not args.bazel_bin:
        args.bazel_bin = os.path.join(
            args.fuchsia_dir, 'prebuilt', 'third_party', 'bazel', host_tag,
            'bazel')

    create_dir(args.output_base_dir)
    create_dir(args.workspace_dir)

    def write_workspace_file(path, content):
        with open(os.path.join(args.workspace_dir, path), 'w') as f:
            f.write(content)

    def create_workspace_symlink(path, dst_path):
        link_path = os.path.join(args.workspace_dir, path)
        force_symlink(dst_path, link_path)

    script_path = os.path.relpath(__file__, args.fuchsia_dir)

    if args.use_bzlmod:
        write_workspace_file(
            'WORKSPACE.bazel', '# Empty on purpose, see MODULE.bazel\n')

        create_workspace_symlink(
            'MODULE.bazel',
            os.path.join(
                args.fuchsia_dir, 'build', 'bazel', 'toplevel.MODULE.bazel'))
    else:
        create_workspace_symlink(
            'WORKSPACE.bazel',
            os.path.join(
                args.fuchsia_dir, 'build', 'bazel', 'toplevel.WORKSPACE.bazel'))

    # Generate symlinks

    def excluded_file(path):
        """Return true if a file path must be excluded from the symlink list."""
        # Never symlink to the 'out' directory.
        if path == "out":
            return True
        # Don't symlink the Jiri files, this can confuse Jiri during an 'jiri update'
        if path.startswith('.jiri'):
            return True
        return False

    topfiles = [
        path for path in os.listdir(args.fuchsia_dir)
        if not excluded_file(path)
    ]
    for path in topfiles:
        create_workspace_symlink(path, os.path.join(args.fuchsia_dir, path))

    create_workspace_symlink(
        'BUILD.bazel',
        os.path.join(
            args.fuchsia_dir, 'build', 'bazel', 'toplevel.BUILD.bazel'))

    create_workspace_symlink(
        '.bazelrc',
        os.path.join(args.fuchsia_dir, 'build', 'bazel', 'toplevel.bazelrc'))

    # Create a symlink to the GN-generated file that will contain the list
    # of @legacy_ninja_build_outputs entries. This file is generated by the
    # GN target //build/bazel:legacy_ninja_build_outputs.
    create_workspace_symlink(
        'bazel_inputs_manifest.json',
        os.path.join(
            args.workspace_dir, '..',
            'legacy_ninja_build_outputs.inputs_manifest.json'))

    # Generate wrapper script in topdir/bazel that invokes Bazel with the right --output_base.
    bazel_wrapper = r'''#!/bin/bash
# Auto-generated - DO NOT EDIT!
readonly _SCRIPT_DIR="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" >/dev/null 2>&1 && pwd)"
readonly _WORKSPACE_DIR="${{_SCRIPT_DIR}}/{workspace}"
readonly _OUTPUT_BASE="${{_SCRIPT_DIR}}/{output_base}"

# Exported explicitly to be used by repository rules to reference the
# Ninja output directory and binary.
export BAZEL_FUCHSIA_NINJA_OUTPUT_DIR="{ninja_output_dir}"
export BAZEL_FUCHSIA_NINJA_PREBUILT="{ninja_prebuilt}"

cd "${{_WORKSPACE_DIR}}" && {bazel_bin_path} \
      --nohome_rc \
      --output_base="${{_OUTPUT_BASE}}" "$@"
'''.format(
        ninja_output_dir=os.path.abspath(os.getcwd()),
        ninja_prebuilt=os.path.abspath(ninja_binary),
        bazel_bin_path=os.path.abspath(args.bazel_bin),
        workspace=os.path.relpath(args.workspace_dir, args.topdir),
        output_base=os.path.relpath(args.output_base_dir, args.topdir))

    bazel_wrapper_path = os.path.join(args.topdir, 'bazel')
    with open(bazel_wrapper_path, 'w') as f:
        f.write(bazel_wrapper)
    os.chmod(
        bazel_wrapper_path, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR |
        stat.S_IRGRP | stat.S_IXGRP)

    # Done!
    return 0


if __name__ == "__main__":
    sys.exit(main())
