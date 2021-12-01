#!/usr/bin/env python3.8

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generates hermeitc inputs file for the corresponding go_build action.

import argparse
import glob
import os
import sys

from gen_library_metadata import get_sources


def go_dep_files_inputs(go_dep_files, is_test):
    """Collect all inputs from go_dep files."""
    inputs = []
    for _, src in get_sources(go_dep_files).items():
        if not os.path.exists(src):
            raise ValueError(f'source "{src}" in go_deps file does not exist')

        if os.path.isfile(src):
            inputs.append(src)
            continue

        # TODO(https://fxbug.dev/80334): always expect files after we deprecate
        # support for globbing.
        #
        # Currently globbing is only used by syzkaller-go (see bug above), which
        # only have .go sources.
        if os.path.isdir(src):
            inputs += glob.glob(os.path.join(src, '**', '*.go'))
            continue

        raise ValueError(
            f'source "{src}" in go_deps file is neither a file or a directory')

    if not is_test:
        inputs = [f for f in inputs if not f.endswith('_test.go')]

    return inputs


# All possible extensions for source files Go knows about.
#
# See https://pkg.go.dev/cmd/go/internal/list#pkg-variables
GO_SRC_EXTS = {
    '.go', '.c', '.cc', '.cxx', '.cpp', '.m', '.h', '.hh', '.hpp', '.hxx', '.f',
    '.F', '.for', '.f90', '.s', '.swig', '.swigcxx', '.syso'
}


def goroot_inputs(goroot):
    """Collect all inputs from goroot."""

    # The Go compiler is an input, it's used by the build script.
    inputs = [os.path.join(goroot, 'bin', 'go')]

    for f in glob.glob(os.path.join(goroot, '**', '*')):
        # Skip all test files from goroot, they won't be used for building.
        if f.endswith('_test.go'):
            continue
        _, ext = os.path.splitext(f)
        if ext in GO_SRC_EXTS:
            inputs.append(f)

    return inputs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--go-dep-files',
        help='List of files describing library dependencies',
        nargs='*',
        default=[])
    parser.add_argument(
        '--output',
        help='Path to output hermetic inputs file',
        required=True,
    )
    parser.add_argument(
        '--go-root',
        help='Path to GOROOT to use',
        required=True,
    )
    parser.add_argument(
        '--is-test', help='True if the target is a go test', default=False)
    args = parser.parse_args()

    inputs = go_dep_files_inputs(args.go_dep_files,
                                 args.is_test) if args.go_dep_files else []
    inputs += goroot_inputs(args.go_root)
    with open(args.output, 'w') as f:
        f.write('\n'.join(sorted(inputs)))


if __name__ == '__main__':
    sys.exit(main())
