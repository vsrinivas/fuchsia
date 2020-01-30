#!/usr/bin/env python2
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#
# Derivative work of https://chromium.googlesource.com/chromium/src/+/refs/heads/master/build/config/fuchsia/prepare_package_inputs.py
#
"""Creates a archive manifest used for Fuchsia package generation."""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile


def make_package_path(file_path, roots):
    """Computes a path for |file_path| relative to one of the |roots|.

  Args:
    file_path: The file path to relativize.
    roots: A list of directory paths which may serve as a relative root for
      |file_path|.

    For example:
        * make_package_path('/foo/bar.txt', ['/foo/']) 'bar.txt'
        * make_package_path('/foo/dir/bar.txt', ['/foo/']) 'dir/bar.txt'
        * make_package_path('/foo/out/Debug/bar.exe', ['/foo/', '/foo/out/Debug/']) 'bar.exe'
  """

    # Prevents greedily matching against a shallow path when a deeper, better
    # matching path exists.
    roots.sort(key=len, reverse=True)

    for next_root in roots:
        if not next_root.endswith(os.sep):
            next_root += os.sep

        if file_path.startswith(next_root):
            relative_path = file_path[len(next_root):]
            return relative_path

    return file_path


def _get_stripped_path(bin_path):
    """Finds the stripped version of |bin_path| in the build output directory.

        returns |bin_path| if no stripped path is found.
  """
    stripped_path = bin_path.replace('lib.unstripped/',
                                     'lib/').replace('exe.unstripped/', '')
    if os.path.exists(stripped_path):
        return stripped_path
    else:
        return bin_path


def _is_binary(path):
    """Checks if the file at |path| is an ELF executable.

        This is done by inspecting its FourCC header.
  """

    with open(path, 'rb') as f:
        file_tag = f.read(4)
    return file_tag == '\x7fELF'


def _write_build_ids_txt(binary_paths, ids_txt_path):
    """Writes an index text file mapping build IDs to unstripped binaries."""

    READELF_FILE_PREFIX = 'File: '
    READELF_BUILD_ID_PREFIX = 'Build ID: '

    # List of binaries whose build IDs are awaiting processing by readelf.
    # Entries are removed as readelf's output is parsed.
    unprocessed_binary_paths = {os.path.basename(p): p for p in binary_paths}

    with open(ids_txt_path, 'w') as ids_file:
        # Create a set to dedupe stripped binary paths in case both the stripped and
        # unstripped versions of a binary are specified.
        stripped_binary_paths = set(map(_get_stripped_path, binary_paths))
        readelf_stdout = subprocess.check_output(
            ['readelf', '-n'] + list(stripped_binary_paths))

        if len(binary_paths) == 1:
            # Readelf won't report a binary's path if only one was provided to the
            # tool.
            binary_shortname = os.path.basename(binary_paths[0])
        else:
            binary_shortname = None

        for line in readelf_stdout.split('\n'):
            line = line.strip()

            if line.startswith(READELF_FILE_PREFIX):
                binary_shortname = os.path.basename(
                    line[len(READELF_FILE_PREFIX):])
                assert binary_shortname in unprocessed_binary_paths

            elif line.startswith(READELF_BUILD_ID_PREFIX):
                # Paths to the unstripped executables listed in "ids.txt" are specified
                # as relative paths to that file.
                unstripped_rel_path = os.path.relpath(
                    os.path.abspath(unprocessed_binary_paths[binary_shortname]),
                    os.path.dirname(os.path.abspath(ids_txt_path)))

                build_id = line[len(READELF_BUILD_ID_PREFIX):]
                ids_file.write(build_id + ' ' + unstripped_rel_path + '\n')
                del unprocessed_binary_paths[binary_shortname]

    # Did readelf forget anything? Make sure that all binaries are accounted for.
    assert not unprocessed_binary_paths


def _parse_component(component_info_file):
    component_info = json.load(open(component_info_file, 'r'))
    return component_info


def _get_app_filename(component_info):
    for c in component_info:
        if c.get('type') == 'dep':
            pos = c.get('source').find(':')
            return c.get('source')[pos + 1:]


def _get_manifest_file(component_info):
    for c in component_info:
        if c.get('type') == 'manifest':
            return str(c.get('source'))


def _build_manifest(args):
    component_info_list = _parse_component(args.json_file)
    binaries = []
    with open(args.manifest_path, 'w') as manifest, \
         open(args.depfile_path, 'w') as depfile:
        for component_info in component_info_list:
            app_filename = _get_app_filename(component_info)

            # Process the runtime deps file for file paths, recursively walking
            # directories as needed.
            # make_package_path() may relativize to either the source root or output
            # directory.
            # runtime_deps may contain duplicate paths, so use a set for
            # de-duplication.
            expanded_files = set()
            for next_path in open(args.runtime_deps_file, 'r'):
                next_path = next_path.strip()
                if os.path.isdir(next_path):
                    for root, _, files in os.walk(next_path):
                        for current_file in files:
                            if current_file.startswith('.'):
                                continue
                            expanded_files.add(os.path.join(root, current_file))
                else:
                    expanded_files.add(os.path.normpath(next_path))

            # Format and write out the manifest contents.
            gen_dir = os.path.normpath(os.path.join(args.out_dir, 'gen'))
            app_found = False
            excluded_files_set = set(args.exclude_file)
            for current_file in expanded_files:
                if _is_binary(current_file):
                    binaries.append(current_file)
                current_file = _get_stripped_path(current_file)

                in_package_path = make_package_path(
                    current_file, [gen_dir, args.root_dir, args.out_dir])
                if in_package_path == app_filename:
                    app_found = True

                if in_package_path in excluded_files_set:
                    excluded_files_set.remove(in_package_path)
                    continue

                manifest.write('%s=%s\n' % (in_package_path, current_file))

            if len(excluded_files_set) > 0:
                raise Exception(
                    'Some files were excluded with --exclude-file, but '
                    'not found in the deps list: %s' %
                    ', '.join(excluded_files_set))

            if not app_found:
                raise Exception(
                    'Could not locate executable inside runtime_deps.')

            # Write meta/package manifest file.
            with open(os.path.join(os.path.dirname(args.manifest_path),
                                   'package'), 'w') as package_json:
                json.dump({'version': '0', 'name': args.app_name}, package_json)

            # Write component manifest file.
            cmx_source = _get_manifest_file(component_info)
            cmx_file_path = os.path.join(
                os.path.dirname(args.manifest_path), args.app_name + '.cmx')
            manifest.write(
                'meta/package=%s\n' %
                os.path.relpath(package_json.name, args.out_dir))

            if not cmx_source:
                # Create a defaullt cmx file for a binary, no sandbox.
                with open(cmx_file_path, 'w') as component_manifest_file:
                    component_manifest = {
                        'program': {
                            'binary': app_filename
                        },
                    }
                json.dump(component_manifest, component_manifest_file)

            else:
                shutil.copy(cmx_source, cmx_file_path)

            manifest.write(
                'meta/%s=%s\n' % (
                    os.path.basename(cmx_file_path),
                    os.path.relpath(cmx_file_path, args.out_dir)))
            depfile.write(
                '%s: %s' % (
                    os.path.relpath(args.manifest_path, args.out_dir), ' '.join(
                        [
                            os.path.relpath(f, args.out_dir)
                            for f in expanded_files
                        ])))

            _write_build_ids_txt(binaries, args.build_ids_file)

    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--root-dir', required=True, help='Build root directory')
    parser.add_argument(
        '--out-dir', required=True, help='Build output directory')
    parser.add_argument('--app-name', required=True, help='Package name')
    parser.add_argument(
        '--runtime-deps-file',
        required=True,
        help='File with the list of runtime dependencies.')
    parser.add_argument(
        '--depfile-path', required=True, help='Path to write GN deps file.')
    parser.add_argument(
        '--exclude-file',
        action='append',
        default=[],
        help='Package-relative file path to exclude from the package.')
    parser.add_argument(
        '--manifest-path', required=True, help='Manifest output path.')
    parser.add_argument(
        '--build-ids-file', required=True, help='Debug symbol index path.')
    parser.add_argument('--json-file', required=True)

    args = parser.parse_args()

    return _build_manifest(args)


if __name__ == '__main__':
    sys.exit(main())
