# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This tool is for invoking by the performance comparison trybots.  It
# is intended for comparing the performance of two versions of
# Fuchsia.  Currently it only compares binary sizes.

import argparse
import json
import os
import sys


def ComparePerf(args):
    # This code is just a placeholder to allow the infra recipe to be
    # tested easily.  It does not compare the performance test results yet.
    # It just prints the filenames of the perf test result files.
    for dir_path in [args.results_dir_before, args.results_dir_after]:
        print '\n-- in directory %r:' % dir_path
        for filename in sorted(os.listdir(dir_path)):
            print filename


def TotalSize(snapshot_file):
    with open(snapshot_file) as fh:
        data = json.load(fh)
    return sum(info['size'] for info in data['blobs'].itervalues())


def CompareSizes(args):
    filenames = [args.snapshot_before, args.snapshot_after]
    sizes = [TotalSize(filename) for filename in filenames]
    print 'Size before:  %d bytes' % sizes[0]
    print 'Size after:   %d bytes' % sizes[1]
    print 'Difference:   %d bytes' % (sizes[1] - sizes[0])
    if sizes[0] != 0:
        print 'Factor of:    %f' % (float(sizes[1]) / sizes[0])


def Main(argv):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    parser_compare_perf = subparsers.add_parser(
        'compare_perf',
        help='Compare two sets of perf test results')
    parser_compare_perf.add_argument('results_dir_before')
    parser_compare_perf.add_argument('results_dir_after')
    parser_compare_perf.set_defaults(func=ComparePerf)

    parser_compare_sizes = subparsers.add_parser(
        'compare_sizes',
        help='Compare file sizes specified by two system.snapshot files')
    parser_compare_sizes.add_argument('snapshot_before')
    parser_compare_sizes.add_argument('snapshot_after')
    parser_compare_sizes.set_defaults(func=CompareSizes)

    args = parser.parse_args(argv)
    args.func(args)


if __name__ == '__main__':
    Main(sys.argv[1:])
