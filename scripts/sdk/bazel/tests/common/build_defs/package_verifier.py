# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--meta',
                        help='The path to the package\'s meta directory',
                        required=True)
    parser.add_argument('--files',
                        help='The list of expected files in the package',
                        default=[],
                        nargs='*')
    parser.add_argument('--stamp',
                        help='The path to the stamp file in case of success',
                        required=True)
    args = parser.parse_args()

    all_files = []
    # List the files in the meta directory itself.
    for root, dirs, files in os.walk(args.meta):
        all_files += [os.path.relpath(os.path.join(root, f), args.meta) for f in files]
    # Add the files outside of the meta directory, which are listed in
    # meta/contents.
    with open(os.path.join(args.meta, 'meta', 'contents')) as contents_file:
        all_files += [l.strip().split('=', 1)[0] for l in contents_file.readlines()]

    has_errors = False
    for file in args.files:
        if file not in all_files:
            print('Missing %s' % file)
            has_errors = True
    if has_errors:
        print('Known files:')
        print(all_files)
        return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Success!')

    return 0


if __name__ == '__main__':
    sys.exit(main())
