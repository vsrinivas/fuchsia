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
  %(changes)s

(tip) To update the signature run:
   $ %(name)s %(dir)s > %(signature)s"""


def main(name, argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', metavar='DIR', help='path to directory')
    parser.add_argument('-c', '--compare', metavar='SIGNATURE',
                        help='path to input signature file')

    args = parser.parse_args(argv)
    signatures = dir_signatures(args.path)

    if not args.compare:
        print(json.dumps(signatures, sort_keys=True, indent=4))
    else:
        try:
            with open(args.compare, 'rb') as file:
                expected = json.loads(file.read())

                current_paths = set(signatures.keys())
                expected_paths = set(expected.keys())

                added = sorted(current_paths - expected_paths)
                removed = sorted(expected_paths - current_paths)
                changed = sorted([path for path in
                                  current_paths.intersection(expected_paths)
                                  if signatures[path] != expected[path]])

                if added or removed or changed:
                    changes = '\n  '.join([p + ' (added)' for p in added] +
                                          [p + ' (changed)' for p in changed] +
                                          [p + ' (removed)' for p in removed])

                    values = {
                        'changes': changes,
                        'name': name,
                        'dir': args.path,
                        'signature': args.compare
                    }

                    sys.exit(MESSAGE % values)
                else:
                    print('signatures match')


        except OSError:
            sys.exit('could not read signature from: %s' % args.compare)


def dir_signatures(path):
    if not os.path.exists(path):
        sys.exit('could not find path: %s' % path)

    signatures = {}
    file_paths = sorted([os.path.join(root, file)
                         for root, _, files in os.walk(path)
                         for file in files])

    for file_path in file_paths:
            try:
                with open(file_path, 'rb') as file:
                    hash = hashlib.sha1()
                    while True:
                        chunk = file.read(CHUNK_SIZE)
                        if not chunk:
                            break
                        hash.update(hashlib.sha1(chunk).hexdigest().encode('utf-8'))

                    relative = os.path.relpath(file_path, path)
                    signatures[relative] = hash.hexdigest()
            except OSError:
                sys.exit('could not read: %s' % path)

    return signatures


if __name__ == "__main__":
    main(sys.argv[0], sys.argv[1:])
