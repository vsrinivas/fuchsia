# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This tool is for invoking by the performance comparison trybots.  It
# is intended for comparing the performance of two versions of
# Fuchsia.  Currently it only compares binary sizes.

import argparse
import json
import sys


def TotalSize(snapshot_file):
    with open(snapshot_file) as fh:
        data = json.load(fh)
    return sum(info['size'] for info in data['blobs'].itervalues())


def Main(argv):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()
    parser_compare = subparsers.add_parser(
        'compare_sizes',
        help='Compare file sizes specified by two system.snapshot files')
    parser_compare.add_argument('snapshot_before')
    parser_compare.add_argument('snapshot_after')
    args = parser.parse_args(argv)

    filenames = [args.snapshot_before, args.snapshot_after]
    sizes = [TotalSize(filename) for filename in filenames]
    print 'Size before:  %d bytes' % sizes[0]
    print 'Size after:   %d bytes' % sizes[1]
    print 'Difference:   %d bytes' % (sizes[1] - sizes[0])
    if sizes[0] != 0:
        print 'Factor of:    %f' % (float(sizes[1]) / sizes[0])


if __name__ == '__main__':
    Main(sys.argv[1:])
