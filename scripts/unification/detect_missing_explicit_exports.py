#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Identifies build files under //zircon containing libraries which do not
# explicitly export their symbols.
# See ticket 31763.

import os
import re
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    SCRIPT_DIR))                 # unification

VISIBILITY_MARKER = 'public/gn/config:visibility_hidden'
LIBRARY_DECLARATION = '^\s*library\("[^"]+"\)'


def main():
    source_dir = os.path.abspath(FUCHSIA_ROOT)
    zircon_dir = os.path.join(source_dir, 'zircon')

    visibility_pattern = re.compile(VISIBILITY_MARKER)
    library_pattern = re.compile(LIBRARY_DECLARATION)

    n_missing = 0
    n_files = 0
    for base, _, files in os.walk(zircon_dir):
        for file in files:
            if file != 'BUILD.gn':
                continue
            path = os.path.join(base, file)
            with open(path, 'r') as build_file:
                lines = build_file.readlines()
            n_visibility = len(filter(lambda l: visibility_pattern.search(l),
                                      lines))
            n_library = len(filter(lambda l: library_pattern.search(l), lines))
            if n_library > n_visibility:
                delta = n_library - n_visibility
                n_missing = n_missing + delta
                n_files = n_files + 1
                print('[%s] %s' % (delta, os.path.relpath(path, source_dir)))

    print('Found %s instance(s) in %s file(s)' % (n_missing, n_files))
    return 0


if __name__ == '__main__':
    sys.exit(main())
