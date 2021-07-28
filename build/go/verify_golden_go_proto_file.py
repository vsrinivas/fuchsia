#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

# Verifies that the current golden go proto file matches the provided golden.
# Intentionally ignores the version numbers of the protoc compiler and plugins
# that are embedded in the files.


def filter_line(line):
    """Filter input .pb.go line to ignore non-problematic differences."""
    # Strip the compiler and plugin version numbers. The expected lines
    # look like:
    #
    #   // <tab>protoc-gen-go v1.26.0\n
    #   // <tab>protoc        v3.12.4\n
    #
    # Note that protoc-gen-go-grpc does not embed its version number
    # in its output, so isn't checked here.
    #
    for version_prefix in ('// \tprotoc ', '// \tprotoc-gen-go '):
        if line.startswith(version_prefix):
            line = version_prefix + '\n'

    # The Golang protobuf URL is being migrated. See https://fxbug.dev/70570.
    line = line.replace(
        'github.com/golang/protobuf', 'google.golang.org/protobuf')
    return line


def read_file(path):
    """Read input .pb.go file into a list of filtered lines."""
    with open(path) as f:
        return [filter_line(l) for l in f.readlines()]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--golden', help='Path to the golden file', required=True)
    parser.add_argument(
        '--current', help='Path to the local file', required=True)
    parser.add_argument(
        '--fuchsia-dir', help='Path to Fuchsia source directory')
    parser.add_argument(
        '--stamp', help='Path to the victory file', required=True)
    args = parser.parse_args()

    golden = read_file(args.golden)
    current = read_file(args.current)

    if golden != current:
        # Compute paths relative to the Fuchsia directory for the messages
        # below.
        fuchsia_dir = args.fuchsia_dir if args.fuchsia_dir else '../..'
        fuchsia_dir = os.path.abspath(fuchsia_dir)
        golden_path = os.path.relpath(args.golden, fuchsia_dir)
        current_path = os.path.relpath(args.current, fuchsia_dir)
        print(
            f'''Error: Golden file mismatch! To print the differences, run:

  diff -burN {current_path} {golden_path}

To acknowledge this change, please run:

  cp {current_path} {golden_path}

''',
            file=sys.stderr)
        return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Golden!\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
