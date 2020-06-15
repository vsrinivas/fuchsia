#!/usr/bin/env python
#
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Creates a compressed archive of unstripped binaries cataloged by
"ids.txt"."""

import argparse
import os
import subprocess
import sys
import tarfile


def main(args):
    parser = argparse.ArgumentParser()
    parser.add_argument('ids_txt', type=str, help='Path to ids.txt files.')
    parser.add_argument(
        '-o',
        '--output_tarball',
        type=str,
        required=True,
        help='Path which the tarball will be written to.')
    parser.add_argument(
        '--build-id-dir',
        type=str,
        required=True,
        action='append',
        help='Directory containing symbols. Can be specified multiple times')
    args = parser.parse_args(args)

    ids_txt = args.ids_txt
    build_ids_archive = tarfile.open(args.output_tarball, 'w:bz2')
    for line in open(ids_txt, 'r'):
        build_id, binary_path = line.strip().split(' ')

        # Archive the unstripped ELF binary, placing it in a hierarchy keyed to the
        # GNU build ID. The binary resides in a directory whose name is the first
        # two characters of the build ID, with the binary file itself named after
        # the remaining characters of the build ID. So, a binary file with the build
        # ID "deadbeef" would be located at the path 'de/adbeef.debug'.
        binary_file = os.path.join(build_id[:2], build_id[2:] + '.debug')
        symbol_source_path = _find_binary_file(binary_file, args.build_id_dir)

        if not symbol_source_path:
            # Check the ids.txt directory for the executable named.
            symbol_source_path = os.path.abspath(
                os.path.join(os.path.dirname(ids_txt), binary_path))

        if not os.path.exists(symbol_source_path):
            raise ValueError(
                "Cannot find symbol source for %s (%s)." %
                (binary_path, binary_file))

        # Don't check zero length files, they exist as placeholders for prebuilts.
        if os.path.getsize(symbol_source_path):
            # Exclude stripped binaries (indicated by their lack of symbol tables).
            readelf_args = ['readelf', '-S', symbol_source_path]
            readelf_output = subprocess.check_output(readelf_args)
            if '.symtab' in readelf_output:
                build_ids_archive.add(symbol_source_path, binary_file)
    return 0


def _find_binary_file(binary_file, build_id_dirs):
    """Look for the binary_file in the list of build_id_dirs."""

    for dir in build_id_dirs:
        filepath = os.path.join(dir, binary_file)
        if os.path.exists(filepath):
            return filepath
    return None


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
