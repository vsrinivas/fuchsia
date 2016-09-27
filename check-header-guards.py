#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to check C and C++ file header guards.

This script accepts a list of file or directory arguments. If a given
path is a file, it runs the checker on it. If the path is a directory,
it runs the checker on all files in that directory.

In addition, this script checks for potential header guard
collisions. This is useful since we munge / to _, and so
    lib/abc/xyz/xyz.h
and
    lib/abc_xyz/xyz.h
both want to use LIB_ABC_XYZ_XYZ_H_ as a header guard.

"""


import argparse
import collections
import os.path
import paths
import re
import sys

all_header_guards = collections.defaultdict(list)

pragma_once = re.compile('^#pragma once$')


def check_file(path):
    """Check whether the file has a correct header guard.

    A header guard can either be a #pragma once, or else a matching set of
        #ifndef PATH_TO_FILE_
        #define PATH_TO_FILE_
        ...
        #endif  // PATH_TO_FILE_
    preprocessor directives, where both '.' and '/' in the path are
    mapped to '_', and a trailing '_' is appended.

    In either the #pragma once case or the header guard case, it is
    assumed that there is no trailing or leading whitespace.

    """

    # Only check .h files
    if path[-2:] != '.h':
        return True

    assert(path.startswith(paths.FUCHSIA_ROOT))
    relative_path = path[len(paths.FUCHSIA_ROOT):].strip('/')
    upper_path = relative_path.upper()
    header_guard = upper_path.replace('.', '_').replace('/', '_') + '_'
    all_header_guards[header_guard].append(path)

    ifndef = re.compile('^#ifndef %s$' % header_guard)
    define = re.compile('^#define %s$' % header_guard)
    endif = re.compile('^#endif +// %s$' % header_guard)

    found_pragma_once = False
    found_ifndef = False
    found_define = False
    found_endif = False

    with open(path, 'r') as f:
        for line in f.readlines():
            match = pragma_once.match(line)
            if match:
                if found_pragma_once:
                    print('%s contains multiple #pragma once' % path)
                    return False
                found_pragma_once = True

            match = ifndef.match(line)
            if match:
                if found_ifndef:
                    print('%s contains multiple ifndef header guards' % path)
                    return False
                found_ifndef = True

            match = define.match(line)
            if match:
                if found_define:
                    print('%s contains multiple define header guards' % path)
                    return False
                found_define = True

            match = endif.match(line)
            if match:
                if found_endif:
                    print('%s contains multiple endif header guards' % path)
                    return False
                found_endif = True

    if found_pragma_once:
        if found_ifndef or found_define or found_endif:
            print('%s contains both #pragma once and header guards' % path)
            return False
        return True

    if found_ifndef and found_define and found_endif:
        return True

    if found_ifndef or found_define or found_endif:
        print('%s contained only part of a header guard' % path)
        return False

    print('%s contained neither a header guard nor #pragma once' % path)
    return False


def check_dir(p):
    """ Walk recursively over a directory checking .h files"""

    def prune(d):
        if d[0] == '.':
            return True
        return False

    for root, dirs, paths in os.walk(p):
        # Prune dot directories like .git
        [dirs.remove(d) for d in list(dirs) if prune(d)]
        for path in paths:
            check_file(os.path.join(root, path))


def check_collisions():
    for header_guard, paths in all_header_guards.iteritems():
        if len(paths) == 1:
            continue
        print('Multiple files could use %s as a header guard:' % header_guard)
        for path in paths:
            print('    %s' % path)


def main():
    for p in sys.argv[1:]:
        p = os.path.abspath(p)
        if os.path.isdir(p):
            check_dir(p)
        else:
            check_file(p)
    check_collisions()


if __name__ == "__main__":
    sys.exit(main())
