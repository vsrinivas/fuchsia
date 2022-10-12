#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import shutil
import sys

# Verifies that the candidate golden go proto file matches the provided golden.
# Intentionally ignores the version numbers of the protoc compiler and plugins
# that are embedded in the files.

MISMATCH_MSG = '''\
Error: Golden file mismatch! To print the differences, run:

  diff -urN {candidate_path} {golden_path}

To acknowledge this change, please run:

  cp {candidate_path} {golden_path}

'''


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
    for version_prefix in ('// \tprotoc ', '// \tprotoc-gen-go '):
        if line.startswith(version_prefix):
            return version_prefix + '\n'

    # Ignore differences in whitespace within comments. For example, the
    # following lines are treated as the same:
    #
    # someCode() // - foo bar baz
    # someCode() //    -    foo bar     baz
    # someCode() //- foo bar baz
    comment_match = re.match(r'^(.*\/\/)(\s*)(.*)$', line)
    if comment_match is not None:
        return (
            comment_match.group(1) +
            re.sub(r'\s+', ' ', comment_match.group(3)))

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
        '--candidate', help='Path to the local file', required=True)
    parser.add_argument(
        '--fuchsia-dir', help='Path to Fuchsia source directory')
    parser.add_argument(
        '--stamp', help='Path to the victory file', required=True)
    parser.add_argument(
        '--update',
        help="Overwrites candidate with golden if they don't match.",
        action='store_true')
    args = parser.parse_args()

    golden = read_file(args.golden)
    candidate = read_file(args.candidate)

    if golden != candidate:
        if args.update:
            shutil.copyfile(args.candidate, args.golden)
        else:
            # Compute paths relative to the Fuchsia directory for the message
            # below.
            fuchsia_dir = args.fuchsia_dir if args.fuchsia_dir else '../..'
            fuchsia_dir = os.path.abspath(fuchsia_dir)
            golden_path = os.path.relpath(args.golden, fuchsia_dir)
            candidate_path = os.path.relpath(args.candidate, fuchsia_dir)
            print(
                MISMATCH_MSG.format(
                    candidate_path=candidate_path, golden_path=golden_path),
                file=sys.stderr)
            return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Golden!\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
