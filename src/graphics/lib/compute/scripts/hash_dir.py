#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
generate hash signature of a directory and optionally compare it to an existing
one

This script reads all files under DIR to generate SHA-1 hashes from their
content. By default, the hash signature is printed and the script exits.

The --compare SIGNATURE option can be used to check the hashes against a fixed
signature file. SIGNATURE should be the path to an input text file containing
one such signature. In this mode, the program's status will be 0 in case of
success, or 1 in case of failure.
"""

import argparse
import hashlib
import json
import os
import sys

SHA1_HEX_LEN = 40
CHUNK_SIZE = 4096
MESSAGE = """signature has changed:
  {changes}

(tip) To update the signature run:
   $ {name} \\\n   --header_paths {paths} \\\n   --header_dir {dir} \\\n   > {signature}"""


def main(name, argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--header_paths',
        required=True,
        nargs='*',
        help='array of paths to header files')
    parser.add_argument(
        '--header_dir', required=True, help='path to header file directory')
    parser.add_argument(
        '--compare', metavar='SIGNATURE', help='path to input signature file')
    parser.add_argument(
        '--stamp', help="path to stamp file to write after completion")

    args = parser.parse_args(argv)
    signatures = dir_signatures(args.header_paths, args.header_dir)

    if not args.compare:
        print(json.dumps(signatures, sort_keys=True, indent=4))
    elif not args.stamp:
        sys.exit('must use --stamp flag if comparing signature files')
    else:
        try:
            with open(args.compare, 'rb') as file:
                try:
                    expected = json.load(file)
                except json.decoder.JSONDecodeError as e:
                    sys.exit(
                        f'{e}\nerror raised while loading json of: {args.compare}'
                    )

                current_paths = set(signatures.keys())
                expected_paths = set(expected.keys())

                added = sorted(current_paths - expected_paths)
                removed = sorted(expected_paths - current_paths)
                changed = sorted(
                    [
                        path
                        for path in current_paths.intersection(expected_paths)
                        if signatures[path] != expected[path]
                    ])

                if added or removed or changed:
                    changes = '\n  '.join(
                        [p + ' (added)' for p in added] +
                        [p + ' (changed)' for p in changed] +
                        [p + ' (removed)' for p in removed])

                    message_values = {
                        'changes':
                            changes,
                        'name':
                            os.path.abspath(name),
                        'paths':
                            ' \\\n     '.join(
                                [
                                    os.path.abspath(rel_path)
                                    for rel_path in args.header_paths
                                ]),
                        'dir':
                            os.path.abspath(args.header_dir),
                        'signature':
                            os.path.abspath(args.compare)
                    }

                    sys.exit(MESSAGE.format(**message_values))
        except OSError:
            sys.exit(f'could not read signature from: {args.compare}')
        with open(args.stamp, 'w') as stamp:
            stamp.truncate()


def dir_signatures(header_paths, header_dir):
    for path in header_paths:
        if not os.path.exists(path):
            sys.exit(f'could not find path: {path}')

    signatures = {}
    file_paths = sorted(header_paths)

    for file_path in file_paths:
        try:
            with open(file_path, 'rb') as file:
                hash = hashlib.sha1()
                while True:
                    chunk = file.read(CHUNK_SIZE)
                    if not chunk:
                        break
                    hash.update(hashlib.sha1(chunk).hexdigest().encode('utf-8'))

                relative = os.path.relpath(file_path, header_dir)
                signatures[relative] = hash.hexdigest()
        except OSError:
            sys.exit(f'could not read: {path}')

    return signatures


if __name__ == "__main__":
    main(sys.argv[0], sys.argv[1:])
