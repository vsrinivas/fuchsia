# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This tool is for invoking by the performance comparison trybots.  It
# is intended for comparing the performance of two versions of
# Fuchsia.  It can also compare binary sizes.

import argparse
import json
import math
import os
import sys


# For comparing results from a performance test, we calculate
# confidence intervals for the mean running times of the test.  If the
# confidence intervals are non-overlapping, we conclude that the
# performance has improved or regressed for this test.
#
# We make the simplifying assumption that the running time for each
# run of a performance test is normally distributed.  (In practice,
# this assumption is not true.  We will need to check how much tests
# deviate from this assumption, and how much that affects the
# comparison we are doing here.)
#
# With that assumption, ideally we should use Student's t-distribution
# for calculating the confidence intervals for the means.  That is
# easy if the SciPy library is available.  However, this code runs
# using infra's copy of Python, which doesn't make SciPy available.
# For now, we instead use the normal distribution for calculating the
# confidence intervals (giving a Z-test instead of a t-test).
# Fortunately, for large samples (i.e. large numbers of test runs),
# the difference between the two is small.


# ALPHA is a parameter for calculating confidence intervals.  It is
# the probability that the true value for the statistic we're
# estimating (here, the mean running time) lies outside the confidence
# interval.
#
# TODO(IN-646): Figure out how to use SciPy with Python on the bots.
# Then we can uncomment ALPHA here and avoid using the pre-calculated
# Z_TEST_OFFSET below.
#
# ALPHA = 0.01

# This is the value of scipy.stats.norm.ppf(ALPHA / 2).
Z_TEST_OFFSET = -2.5758293035489008

# Returns the mean and standard deviation of a sample.  This does the
# same as scipy.stats.norm.fit().  This does not apply Bessel's
# correction to the calculation of the standard deviation.
def MeanAndStddev(values):
    mean_val = float(sum(values)) / len(values)
    sum_of_squares = 0.0
    for val in values:
        diff = val - mean_val
        sum_of_squares += diff * diff
    stddev_val = math.sqrt(sum_of_squares / len(values))
    return mean_val, stddev_val


class Stats(object):

    def __init__(self, values):
        sample_size = len(values)
        mean, stddev = MeanAndStddev(values)
        offset = -Z_TEST_OFFSET * stddev / math.sqrt(sample_size)
        self._mean = mean
        self._offset = offset
        # Confidence interval for the mean.
        self.interval = (mean - offset, mean + offset)

    def FormatConfidenceInterval(self):
        return '%d +/- %d' % (self._mean, self._offset)


def ReadJsonFile(filename):
    with open(filename, 'r') as fh:
        return json.load(fh)


def ResultsFromDir(dir_path):
    results_map = {}
    # Sorting the result of os.listdir() is not essential.  Currently
    # it just makes error handling of duplicates more deterministic.
    for filename in sorted(os.listdir(dir_path)):
        if filename == 'summary.json':
            continue
        if filename.endswith('.json'):
            file_path = os.path.join(dir_path, filename)
            for data in ReadJsonFile(file_path):
                assert data['label'] not in results_map
                results_map[data['label']] = Stats(data['values'])
    return results_map


def FormatTable(rows, out_fh):
    assert len(rows) > 0
    column_count = len(rows[0])
    for row in rows:
        assert len(row) == column_count
    widths = [2 + max(len(row[col_number]) for row in rows)
              for col_number in xrange(column_count)]
    # Underline the header row.  This assumes that the first row is a
    # header row.
    rows.insert(1, ['-' * (width - 2) for width in widths])
    for row in rows:
        for col_number, value in enumerate(row):
            out_fh.write(value)
            if col_number < column_count - 1:
                out_fh.write(' ' * (widths[col_number] - len(value)))
        out_fh.write('\n')


def ComparePerf(args, out_fh):
    results_maps = [ResultsFromDir(args.results_dir_before),
                    ResultsFromDir(args.results_dir_after)]

    # Set of all test case names, including those added or removed.
    labels = set(results_maps[0].iterkeys())
    labels.update(results_maps[1].iterkeys())

    rows = [['Test case', 'Improve/regress?', 'Factor change',
             'Mean before', 'Mean after']]
    for label in sorted(labels):
        if label not in results_maps[0]:
            result = 'added'
            factor_range = '-'
            before_range = '-'
            after_range = results_maps[1][label].FormatConfidenceInterval()
        elif label not in results_maps[1]:
            result = 'removed'
            factor_range = '-'
            before_range = results_maps[0][label].FormatConfidenceInterval()
            after_range = '-'
        else:
            stats = [results_map[label] for results_map in results_maps]
            interval_before = stats[0].interval
            interval_after = stats[1].interval
            factor_min = interval_after[0] / interval_before[1]
            factor_max = interval_after[1] / interval_before[0]
            if interval_after[0] >= interval_before[1]:
                result = 'slower'
            elif interval_after[1] <= interval_before[0]:
                result = 'faster'
            else:
                result = 'no_sig_diff'
            before_range = stats[0].FormatConfidenceInterval()
            after_range = stats[1].FormatConfidenceInterval()
            factor_range = '%.3f-%.3f' % (factor_min, factor_max)
        rows.append([
            label,
            result,
            factor_range,
            before_range,
            after_range])
    FormatTable(rows, out_fh)


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


def Main(argv, out_fh):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    parser_compare_perf = subparsers.add_parser(
        'compare_perf',
        help='Compare two sets of perf test results')
    parser_compare_perf.add_argument('results_dir_before')
    parser_compare_perf.add_argument('results_dir_after')
    parser_compare_perf.set_defaults(
        func=lambda args: ComparePerf(args, out_fh))

    parser_compare_sizes = subparsers.add_parser(
        'compare_sizes',
        help='Compare file sizes specified by two system.snapshot files')
    parser_compare_sizes.add_argument('snapshot_before')
    parser_compare_sizes.add_argument('snapshot_after')
    parser_compare_sizes.set_defaults(func=CompareSizes)

    args = parser.parse_args(argv)
    args.func(args)


if __name__ == '__main__':
    Main(sys.argv[1:], sys.stdout)
