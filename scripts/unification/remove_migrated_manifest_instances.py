#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import fileinput
import os
import re
import sys

from common import (FUCHSIA_ROOT, fx_format)


def main():
    parser = argparse.ArgumentParser(
            description='Removes references to `migrated_manifest` from BUILD.gn files')
    parser.add_argument('--root',
                        help='Path to the directory to inspect',
                        default=FUCHSIA_ROOT)
    args = parser.parse_args()

    build_files = []
    for base, _, files in os.walk(args.root):
        for file in files:
            if file == 'BUILD.gn':
                build_files.append(os.path.join(base, file))

    for build_path in build_files:
        # Number of currently open curly brackets while processing a target to
        # remove.
        # A lesser than or equal to 0 number means no target is currently being
        # erased.
        curly_bracket_depth = 0
        modified = False
        for line in fileinput.FileInput(build_path, inplace=True):
            if '//build/unification/images/migrated_manifest.gni' in line:
                continue
            target_match = re.match('\s*migrated_manifest\(', line)
            if target_match:
                curly_bracket_depth = 1
                modified = True
                continue
            if curly_bracket_depth > 0:
                curly_bracket_depth += line.count('{') - line.count('}')
                if curly_bracket_depth >= 0:
                    # Keep erasing.
                    continue
            sys.stdout.write(line)
        if modified:
            fx_format(build_path)

    return 0


if __name__ == '__main__':
    sys.exit(main())
