# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This tool is for invoking by the performance comparison trybots.  It
# is intended for comparing the performance of two versions of
# Fuchsia.

import argparse
import glob
import json
import math
import os
import subprocess
import sys
import tarfile

import scipy.stats


# For comparing results from a performance test, we calculate
# confidence intervals for the mean running times of the test.  If the
# confidence intervals are non-overlapping, we conclude that the
# performance has improved or regressed for this test.
#
# Data is gathered from a 3-level sampling process:
#
#  1) Boot Fuchsia multiple times.
#  2) For each boot, launch the perf test process one or more times.
#  3) For each process launch, instantiate the performance test and
#     run the body of the test some number of times.
#
# This is intended to account for variation across boots and across process
# launches.
#
# Currently we use t-test confidence intervals.  This assumes that the
# values we apply the t-test to are normally distributed, or approximately
# normally distributed.  In future we could instead use bootstrap
# confidence intervals, which would avoid that assumption.


# Dataset types:
#
# There are four types of dataset containing raw perf test results:
#
#  * Process dataset: JSON data from a single *.fuchsiaperf.json file,
#    which is usually from a single process launch.  This may contain
#    results from multiple test cases.
#
#  * Boot dataset: Data from a single boot of Fuchsia.  This may contain
#    multiple process datasets.
#
#  * Multi-boot dataset: Data from multiple boots of a single build of
#    Fuchsia.  This may contain multiple boot datasets.
#
#  * Before/after dataset: Contains two multi-boot datasets, one from a
#    "before" build of Fuchsia and one from an "after" build.
#
# Note that we use the term "dataset" rather than "results" because the
# former makes it easier to disambiguate using singular vs. plural.  For
# example, "boot_results" is ambiguous as to whether it represents a single
# boot or whether it is a list where each entry is a "boot result"
# representing a single boot.  In contrast, "boot_dataset" (always a single
# instance) vs. "boot_datasets" (always a list or iterable) avoids that
# ambiguity.
#
# The infra recipe represents those datasets on the filesystem as follows:
#
#  * Process dataset: a single .fuchsiaperf.json file.
#
#  * Boot dataset: a directory containing files with names of the following
#    forms:
#
#      <test-executable-name>_process<number>.fuchsiaperf.json - process dataset
#      <test-executable-name>_process<number>.catapult_json - ignored here
#      summary.json - ignored here
#
#    The code below can read a boot dataset from tar files as well as from
#    directories.  Accepting tar files is a convenience for when doing
#    local testing of the statistics (including for validate_perfcompare).
#    The Swarming system used for the bots produces "out.tar" files as
#    results.
#
#  * Multi-boot dataset: a directory containing a "by_boot" subdirectory,
#    which contains boot dataset directories.
#
# A before/after dataset is represented as two directories.


# ALPHA is a parameter for calculating confidence intervals.  It is
# the probability that the true value for the statistic we're
# estimating (here, the mean running time) lies outside the confidence
# interval.
ALPHA = 0.01


def Mean(values):
    if len(values) == 0:
        raise AssertionError('Mean is not defined for an empty sample')
    return float(sum(values)) / len(values)


# Returns the mean and standard deviation of a sample.  This applies
# Bessel's correction to the calculation of the standard deviation.
#
# If the sample contains only a single value, this returns None for the
# standard deviation, because we cannot estimate the standard deviation
# with Bessel's correction in that case.
def MeanAndStddev(values):
    mean_val = Mean(values)
    if len(values) == 1:
        return mean_val, None
    sum_of_squares = 0.0
    for val in values:
        diff = val - mean_val
        sum_of_squares += diff * diff
    stddev_val = math.sqrt(sum_of_squares / (len(values) - 1))
    return mean_val, stddev_val


def FormatDecimal(val, decimal_places):
    return ('%%.%df' % decimal_places) % val


# Format the given "value +/- offset" confidence interval as a string.
#
# This prints a number of decimal fraction digits that is appropriate to
# the width of the confidence interval.  The offset part is formatted to 2
# significant figures.  The value part is formatted with the same number of
# decimal places as the offset.
def FormatConfidenceInterval(value, offset):
    if math.isinf(offset) or math.isnan(offset) or offset <= 0:
        return '%g +/- %g' % (value, offset)
    significant_figures = 2
    # Applying math.floor() ensures that powers of 10 and non powers of 10
    # (e.g. 0.10 and 0.11) are both formatted with the same number of
    # decimal places.
    log_value = int(math.floor(math.log10(offset)))
    decimal_places = max(significant_figures - log_value - 1, 0)
    return '%s +/- %s' % (FormatDecimal(value, decimal_places),
                          FormatDecimal(offset, decimal_places))


class Stats(object):

    def __init__(self, values, unit):
        self._unit = unit
        sample_size = len(values)
        mean, stddev = MeanAndStddev(values)
        self._mean = mean
        if stddev is None:
            self._offset = None
            self.interval = None
        else:
            self._offset = (-scipy.stats.t.ppf(ALPHA / 2, sample_size - 1)
                            * stddev / math.sqrt(sample_size))
            # Confidence interval for the mean.
            self.interval = (mean - self._offset, mean + self._offset)

    def FormatConfidenceInterval(self):
        if self._offset is None:
            # Point estimate only: We cannot calculate a confidence
            # interval because the sample only contained a single value.
            return '%s %s' % (self._mean, self._unit)
        return '%s %s' % (FormatConfidenceInterval(self._mean, self._offset),
                          self._unit)

    # Returns the relative CI width, which is the width of the confidence
    # interval divided by the mean.
    def RelativeConfidenceIntervalWidth(self):
        assert self._offset is not None
        return self._offset * 2 / self._mean


def StatsFormatConfidenceInterval(stats):
    if stats is None:
        return '-'
    return stats.FormatConfidenceInterval()


def ReadJsonFile(filename):
    with open(filename, 'r') as fh:
        return json.load(fh)


def IsResultsFilename(name):
    return name.endswith('.fuchsiaperf.json')


class SingleBootDataset(object):

    def __init__(self, filename):
        self._filename = filename

    def GetProcessDatasets(self):
        # Note that sorting the filename listing (from os.walk() or from
        # tarfile) is not essential, but it helps to make any later processing
        # more deterministic.
        if os.path.isfile(self._filename):
            # Read from tar file.
            with tarfile.open(self._filename) as tar:
                for member in sorted(tar.getmembers(),
                                     key=lambda member: member.name):
                    if IsResultsFilename(member.name):
                        yield json.load(tar.extractfile(member))
        else:
            # Read from directory.
            for dir_path, _, file_names in sorted(os.walk(self._filename)):
                for name in sorted(file_names):
                    if IsResultsFilename(name):
                        yield ReadJsonFile(os.path.join(dir_path, name))


class MultiBootDataset(object):

    def __init__(self, dir_path):
        self._dir_path = dir_path

    def GetBootDatasets(self):
        by_boot_dir = os.path.join(self._dir_path, 'by_boot')
        assert os.path.exists(by_boot_dir), by_boot_dir
        for name in sorted(os.listdir(by_boot_dir)):
            yield SingleBootDataset(os.path.join(by_boot_dir, name))


# Takes a list of values that are collected from consecutive runs of a
# test.  For libperftest tests, those are test runs within a process.
#
# Returns the mean of the values, but excluding the first run.  We treat
# the initial run as a warmup run.  The initial run is often slower than
# later runs, so it would skew the mean if we included it.  The
# RoundTrip_*_MultiProcess tests are an extreme case, because the first run
# waits for a subprocess to start up.  See https://crbug.com/fuchsia/23105.
def MeanExcludingWarmup(values):
    # Some tests report a single value per process run.  For those tests,
    # we use that value and don't discard it.
    if len(values) == 1:
        return values[0]
    return Mean(values[1:])


def FormatTestName(results):
    return '%s: %s' % (results['test_suite'], results['label'])


UNIT_ABBREVIATIONS = {
    'milliseconds': 'ms',
    'nanoseconds': 'ns'}


def FormatUnit(unit_set):
    assert len(unit_set) > 0
    if len(unit_set) > 1:
        raise AssertionError('Inconsistent units for test case: %s' % unit_set)
    unit = list(unit_set)[0]
    return UNIT_ABBREVIATIONS.get(unit, unit)


# Takes a sequence of boot datasets and produces summary statistics.
# Returns a dict mapping test names to Stats objects.
def StatsFromBootDatasets(boot_datasets):
    # Mapping from test names to lists of values.
    results_map = {}
    # Mapping from test names to sets of strings (for units of measurement).
    units_map = {}
    for boot_dataset in boot_datasets:
        results_for_boot = {}
        for process_dataset in boot_dataset.GetProcessDatasets():
            for test_case in process_dataset:
                new_value = MeanExcludingWarmup(test_case['values'])
                name = FormatTestName(test_case)
                results_for_boot.setdefault(name, []).append(new_value)
                units_map.setdefault(name, set()).add(test_case['unit'])
        for label, values in results_for_boot.iteritems():
            results_map.setdefault(label, []).append(Mean(values))
    return {name: Stats(values, FormatUnit(units_map[name]))
            for name, values in results_map.iteritems()}


def StatsFromMultiBootDataset(multi_boot_dataset):
    return StatsFromBootDatasets(multi_boot_dataset.GetBootDatasets())


def FormatFactor(val_before, val_after):
    # Avoid division by zero.
    if val_before == 0:
        return 'inf'
    return '%.3f' % (val_after / val_before)


def FormatFactorRange(interval_before, interval_after):
    if interval_before == (0, 0) and interval_after == (0, 0):
        return 'no_change'
    if interval_before[0] < 0 or interval_after[0] < 0:
        return 'ci_too_wide'
    factor_min = FormatFactor(interval_before[1], interval_after[0])
    factor_max = FormatFactor(interval_before[0], interval_after[1])
    return '%s-%s' % (factor_min, factor_max)


def FormatTable(heading_row, rows, out_fh):
    column_count = len(heading_row)
    for row in rows:
        assert len(row) == column_count
    rows = [heading_row] + rows
    widths = [2 + max(len(row[col_number]) for row in rows)
              for col_number in xrange(column_count)]
    # Underline the heading row.
    rows.insert(1, ['-' * (width - 2) for width in widths])
    for row in rows:
        for col_number, value in enumerate(row):
            out_fh.write(value)
            if col_number < column_count - 1:
                out_fh.write(' ' * (widths[col_number] - len(value)))
        out_fh.write('\n')


def CompareIntervals(stats_before, stats_after):
    assert stats_before is not None or stats_after is not None
    if stats_before is None:
        return 'added', '-'
    if stats_after is None:
        return 'removed', '-'
    if stats_before.interval is None or stats_after.interval is None:
        return 'point_estimate', '-'
    # Using a ">" comparison rather than ">=" ensures that if the intervals
    # are equal and zero-width, they are treated as "no_sig_diff".
    if stats_after.interval[0] > stats_before.interval[1]:
        result = 'slower'
    elif stats_after.interval[1] < stats_before.interval[0]:
        result = 'faster'
    else:
        result = 'no_sig_diff'
    factor_range = FormatFactorRange(stats_before.interval,
                                     stats_after.interval)
    return result, factor_range


def ComparePerf(args, out_fh):
    results_maps = [
        StatsFromMultiBootDataset(MultiBootDataset(dir_path))
        for dir_path in args.results_dir]

    # Set of all test case names, including those added or removed.
    labels = set()
    for results_map in results_maps:
        labels.update(results_map.iterkeys())

    if len(results_maps) != 2:
        # Display the dataset(s) without doing any comparison.
        heading_row = ['Test case']
        if len(results_maps) == 1:
            heading_row.extend(['Mean'])
        else:
            heading_row.extend(['Mean %d' % (idx + 1)
                                for idx in xrange(len(results_maps))])
        rows = []
        for label in sorted(labels):
            row = [label]
            for results_map in results_maps:
                row.append(
                    StatsFormatConfidenceInterval(results_map.get(label)))
            rows.append(row)
        FormatTable(heading_row, rows, out_fh)
        return

    counts = {
        'added': 0,
        'removed': 0,
        'faster': 0,
        'slower': 0,
        'no_sig_diff': 0,
        'point_estimate': 0,
    }
    heading_row = ['Test case', 'Improve/regress?', 'Factor change',
                   'Mean before', 'Mean after']
    all_rows = []
    diff_rows = []
    for label in sorted(labels):
        stats = [results_map.get(label) for results_map in results_maps]
        result, factor_range = CompareIntervals(stats[0], stats[1])
        counts[result] += 1
        row = [label, result, factor_range,
               StatsFormatConfidenceInterval(stats[0]),
               StatsFormatConfidenceInterval(stats[1])]
        all_rows.append(row)
        if result not in ('no_sig_diff', 'point_estimate'):
            diff_rows.append(row)

    def FormatCount(count, text):
        noun = 'test case' if count == 1 else 'test cases'
        out_fh.write('  %d %s %s\n' % (count, noun, text))

    out_fh.write('Summary counts:\n')
    FormatCount(len(labels), 'in total')
    FormatCount(counts['no_sig_diff'],
                'had no significant difference (no_sig_diff)')
    if counts['point_estimate']:
        FormatCount(counts['point_estimate'],
                    'cannot be compared because we have point estimates only')
    FormatCount(counts['faster'], 'got faster')
    FormatCount(counts['slower'], 'got slower')
    FormatCount(counts['added'], 'added')
    FormatCount(counts['removed'], 'removed')
    out_fh.write('\n\n')

    if len(diff_rows) != 0:
        out_fh.write('Results from test cases with differences:\n\n')
        FormatTable(heading_row, diff_rows, out_fh)
        out_fh.write('\n\n')

    out_fh.write('Results from all test cases:\n\n')
    FormatTable(heading_row, all_rows, out_fh)


def PrintMultibootDatasetTable(multiboot_dataset, out_fh):
    stats_map = StatsFromMultiBootDataset(multiboot_dataset)
    heading_row = ['Test case', 'Mean']
    rows = []
    for name, stats in sorted(stats_map.iteritems()):
        rows.append([name, stats.FormatConfidenceInterval()])
    FormatTable(heading_row, rows, out_fh)


def RunLocal(args, out_fh, run_cmd):
    if glob.glob(args.iter_file) != []:
        # We check for this case so that we don't accidentally treat
        # pre-existing files the same as files newly outputted by
        # args.iter_cmd.
        raise AssertionError(
            'Temporary output file(s) %r already exist: try deleting them first'
            % args.iter_file)
    if os.path.exists(args.dest):
        raise AssertionError(
            'Destination path %r already exists: either delete it or use'
            ' a different destination, because run_local will not'
            ' overwrite it or append to it' % args.dest)

    by_boot_dir = os.path.join(args.dest, 'by_boot')
    os.mkdir(args.dest)
    os.mkdir(by_boot_dir)

    for boot_idx in xrange(args.boots):
        # This prefix enables error-checking in the shell commands, for
        # both safety and convenience.
        errexit_prefix = 'set -o errexit -o nounset; '
        run_cmd(errexit_prefix + args.reboot_cmd, shell=True)
        run_cmd(errexit_prefix + args.iter_cmd, shell=True)

        boot_dir = os.path.join(by_boot_dir, 'boot%06d' % boot_idx)
        os.mkdir(boot_dir)
        dataset_files = sorted(glob.glob(args.iter_file))
        for idx, dataset_file in enumerate(dataset_files):
            new_filename = os.path.join(
                boot_dir, 'results%06d.fuchsiaperf.json' % idx)
            os.rename(dataset_file, new_filename)

        # Print a table of the results so far.  This prints confidence
        # intervals, which requires having results from at least 2 boots.
        if boot_idx >= 1:
            out_fh.write('\nResults after %d boots:\n\n' % (boot_idx + 1))
            PrintMultibootDatasetTable(MultiBootDataset(args.dest), out_fh)
            out_fh.write('\n')


def IntervalsIntersect(interval1, interval2):
    return not (interval2[0] >= interval1[1] or
                interval2[1] <= interval1[0])


# Calculate the rate at which two intervals drawn (without replacement)
# from the given set of intervals will be non-intersecting.
def MismatchRate(intervals):
    mismatch_count = sum(int(not IntervalsIntersect(intervals[i], intervals[j]))
                         for i in xrange(len(intervals))
                         for j in xrange(i))
    comparisons_count = len(intervals) * (len(intervals) - 1) / 2
    return float(mismatch_count) / comparisons_count


def ValidatePerfCompare(args, out_fh):
    boot_datasets = [SingleBootDataset(filename)
                     for filename in args.results_dirs]
    boot_count = len(boot_datasets)
    group_size = args.group_size
    group_count = boot_count / group_size

    results_maps = [
        StatsFromBootDatasets(
            boot_datasets[i * group_size : (i + 1) * group_size])
        for i in xrange(group_count)]

    # Group by test name (label).
    by_test = {}
    for results_map in results_maps:
        for label, stats in results_map.iteritems():
            by_test.setdefault(label, []).append(stats)

    out_fh.write('Rate of mismatches (non-intersections) '
                 'of confidence intervals for each test:\n')
    mismatch_rates = []
    for label, stats_list in sorted(by_test.iteritems()):
        mismatch_rate = MismatchRate([stats.interval for stats in stats_list])
        out_fh.write('%f %s\n' % (mismatch_rate, label))
        mismatch_rates.append(mismatch_rate)

    mean_relative_ci_width = Mean([
        stats.RelativeConfidenceIntervalWidth()
        for results_map in results_maps
        for stats in results_map.itervalues()])

    out_fh.write('\n')
    mean_val = Mean(mismatch_rates)
    out_fh.write('Mean mismatch rate: %f\n' % mean_val)
    out_fh.write('Mean relative confidence interval width: %f\n'
                 % mean_relative_ci_width)
    out_fh.write('Number of test cases: %d\n' % len(mismatch_rates))
    out_fh.write('Number of result sets: %d groups of %d boots each'
                 ' (ignoring %d leftover boots)\n'
                 % (group_count, group_size,
                    boot_count - group_size * group_count))
    out_fh.write('Expected number of test cases with mismatches: %f\n'
                 % (mean_val * len(mismatch_rates)))


def Main(argv, out_fh, run_cmd=subprocess.check_call):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    subparser = subparsers.add_parser(
        'compare_perf',
        help='Display sets of perf test results. '
        ' If given two datasets, this will compare the two, showing whether'
        ' tests had regressions or improvements. '
        ' Otherwise (if given 1 or >2 datasets), the data is shown with no'
        ' comparisons.')
    subparser.add_argument('results_dir', nargs='+')
    subparser.set_defaults(func=lambda args: ComparePerf(args, out_fh))

    subparser = subparsers.add_parser(
        'run_local',
        help='Gather a multi-boot dataset of performance test results'
        ' from a single version of Fuchsia by locally running the command'
        ' specified by --iter_cmd')
    subparser.add_argument(
        '--boots', type=int, required=True,
        help='Number of (re)boots of Fuchsia to run')
    subparser.add_argument(
        '--iter_cmd', required=True,
        help='Command for running a performance test. '
        ' This command is run locally: it is passed to the shell. '
        ' This command is expected to write its output to the file (or files)'
        ' specified by --iter_file. '
        ' Note that error-checking is enabled for this shell command (using'
        ' "set -o errexit -o nounset")')
    subparser.add_argument(
        '--iter_file', required=True,
        help='File(s) that the performance test will write its results to. '
        ' This is a glob expression, so it may specify multiple files. '
        ' Each file is expected to be a process dataset in the'
        ' *.fuchsiaperf.json format.  These files will be removed (renamed)'
        ' by this tool')
    subparser.add_argument(
        '--reboot_cmd', default='fx reboot && fx wait',
        help='Command to use for rebooting Fuchsia.  This is optional. '
        ' The default is %(default)r.  As with --iter_cmd, error-checking is'
        ' enabled for this shell command')
    subparser.add_argument(
        '--dest', required=True,
        help='Destination directory for writing the multi-boot dataset')
    subparser.set_defaults(func=lambda args: RunLocal(args, out_fh, run_cmd))

    subparser = subparsers.add_parser(
        'validate_perfcompare',
        help='Outputs statistics given multiple sets of perf test results'
        ' that come from the same build.  This is for validating the'
        ' statistics used by the perfcompare tool.  It can be used to check'
        ' the rate at which the tool will falsely indicate that performance'
        ' of a test case has regressed or improved.')
    subparser.add_argument(
        '-g', '--group_size', type=int, required=True,
        help='Number of boots to put in each group.  To get realistic'
        ' results that reflect how the perfcompare trybots would behave,'
        ' this should match the boots_per_revision setting in the'
        ' infra recipe.  (Since that code is currently not part of the'
        ' Fuchsia checkout, we cannot make the settings match'
        ' automatically.)')
    subparser.add_argument('results_dirs', nargs='+')
    subparser.set_defaults(func=lambda args: ValidatePerfCompare(args, out_fh))

    args = parser.parse_args(argv)
    args.func(args)


if __name__ == '__main__':
    Main(sys.argv[1:], sys.stdout)
