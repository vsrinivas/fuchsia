#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Test
### Query and calculate test stats.

import argparse
import sys
import os
import subprocess
import json
from collections import defaultdict
import random
import time


# Retrieve stats on test execution.
#
# Example: fx test-stats --print
#          (Prints a categorization of all tests in the current build.)
#
# Example: fx test-stats --print --os fuchsia
#          (Prints a categorization of Fuchsia tests in the current build.)
#
# Example: fx test-stats --run --os fuchsia
#          (Run all Fuchsia tests on a device.)
#
# Example: fx test-stats --run --dimension device_type=AEMU
#          (Run only tests with device_type AEMU.)
#
# Example: fx test-stats --run --type v2
#          (Run only components v2 tests on a device.)
#
# Example: fx test-stats --run --type v2 --parallel 4
#          (Run components v2 tests with up to 4 cases running concurrently.)
#
# Example: fx test-stats --run --concurrent 4
#          (Run all tests with up to 4 test suites running concurrently.)
#
# Example: fx test-stats --run --timeout 5
#          (Run all tests with a maximum allowed running time of 5 seconds.)
def main():
    parser = argparse.ArgumentParser('Retrieve stats on test execution')
    action_group = parser.add_mutually_exclusive_group(required=True)
    action_group.add_argument(
        '--print',
        action='store_true',
        help='Print a count of matching tests by their various properties')
    action_group.add_argument(
        '--run', action='store_true', help='Run all of the matching tests once')
    filter_group = parser.add_argument_group('filter')
    filter_group.add_argument(
        '--dimension',
        action='append',
        help='Include only tests matching given dimensions; can be specified multiple times. '
        'Example: --dimension os=Linux --dimension cpu=x64')
    filter_group.add_argument(
        '--os',
        action='store',
        help='Include only tests matching the given os. Example: --os fuchsia')
    filter_group.add_argument(
        '--type',
        action='store',
        help='Include only tests of this type. Example: --type v2')
    run_group = parser.add_argument_group('run')
    run_group.add_argument(
        '-c',
        '--concurrent',
        action='store',
        type=int,
        default=1,
        help='Number of tests to run concurrently. Default is 1.',
    )
    run_group.add_argument(
        '-t',
        '--timeout',
        action='store',
        type=float,
        default=120,
        help='Timeout for tests, in seconds. Default is 120.',
    )
    run_group.add_argument(
        '-p',
        '--parallel',
        action='store',
        type=int,
        default=None,
        help='Number of test cases per v2 suite to run in parallel. Uses runner default if not set.',
    )
    run_group.add_argument(
        '--shuffle',
        action='store',
        type=int,
        default=None,
        help='Toggle shuffling input. If set to 0, the current timestamp is used as the seed. If set no a non-zero number, that value will be used as the shuffle seed',
    )
    run_group.add_argument(
        '--count',
        action='store',
        type=int,
        help='If set, only run this number of tests. The first tests in order following any shuffling will be used'
    )
    args = parser.parse_args()

    test_tuples = get_tests_tuples(args)

    if args.print:
        print_stats(test_tuples)
    elif args.run:
        run_tests(
            test_tuples,
            timeout_seconds=args.timeout,
            max_running_tests=args.concurrent,
            parallel=args.parallel)
    else:
        print('Unknown mode.')
        parser.print_help()
        return 1

    return 0


# Loads test definitions, parses them, and returns only those matching the argument filters.
#
# Returns a list of tuples (parsed_test, original_test_json)
def get_tests_tuples(args):
    json_data = json.loads(
        subprocess.check_output(['fx', 'test', '--printtests']))

    dimension_pairs = None
    if args.dimension:
        dimension_pairs = set([
            (s[0], s[1])
            for s in map(lambda x: x.split('='), args.dimension)
            if len(s) > 1
        ])

    def to_include(val):
        (parsed_test, test_json) = val
        if parsed_test is None:
            return False

        ret = True
        if dimension_pairs is not None:
            # Only include this test if one of its set of dimensions is completely included in the specified dimensions.
            if not any(
                    all(
                        map(lambda v: v in dimension_pairs,
                            e['dimensions'].items()))
                    for e in test_json['environments']):
                ret = False
        if args.os is not None:
            if test_json['test']['os'] != args.os:
                ret = False
        if args.type is not None:
            if parsed_test.test_type != args.type:
                ret = False

        return ret

    test_list = list(
        filter(to_include, map(lambda x: (parse_test(x), x), json_data)))

    if args.shuffle is not None:
        shuffle = args.shuffle
        if shuffle == 0:
            shuffle = None
        random.seed(shuffle)
        random.shuffle(test_list)
    if args.count is not None:
        test_list = test_list[:args.count]

    return test_list


# Wraps a test parsed from `fx test --printtests`
class Test:

    def __init__(self, test_type=None, path=None, package_url=None):
        self.test_type = test_type
        self.path = path
        self.package_url = package_url

    # Get a unique key for this test
    def key(self):
        return self.package_url or self.path

    def __str__(self):
        return '{} test "{}"'.format(self.test_type, self.package_url or
                                     self.path)


# Parse a single JSON dict into a Test class.
def parse_test(test):
    test_val = test['test']
    if 'package_url' in test_val:
        suffix = test_val['package_url'][-3:]
        if suffix == 'cmx':
            return Test(test_type='v1', package_url=test_val['package_url'])
        elif suffix == '.cm':
            return Test(test_type='v2', package_url=test_val['package_url'])
        else:
            return 'unknown package'
    elif 'path' in test_val:
        return Test(test_type='host', path=test_val['path'])
    else:
        return Test(test_type='unknown')


# Implementation for --print mode.
def print_stats(test_tuples):
    dimension_counts = defaultdict(int)
    type_counts = defaultdict(int)
    for (parsed_test, test_json) in test_tuples:
        for env in test_json['environments']:
            lst = []
            for name, value in env['dimensions'].items():
                lst.append('{}: {}'.format(name, value))
            lst.sort()
            dimension_counts['({})'.format(', '.join(lst))] += 1
        type_counts[parsed_test.test_type] += 1

    print('Dimensions:')
    for name, count in dimension_counts.items():
        print('  {}: {}'.format(name, count))
    print('Types:')
    for name, count in type_counts.items():
        print('  {}: {}'.format(name, count))


# Wrapper for a test that is currently running on a device.
#
# This class keeps track of the start and end times of the test run.
class StartedTest:

    # Start executing the given command line and wrap it in a StartedTest.
    @staticmethod
    def create(command_line):
        return StartedTest(
            command_line, time.time(),
            subprocess.Popen(
                command_line,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL))

    def __init__(self, command_line, start_time, running_process):
        self._command_line = command_line
        self._start_time = start_time
        self._running_process = running_process
        self._end_time = None

    # Check if the test is done, updating internal state.
    #
    # This function must be called periodically to determine when the test is complete.
    #
    # Returns True if the test is done, False otherwise.
    def poll_done(self):
        if self._end_time is not None:
            return True

        if self._running_process.poll() is not None:
            self._end_time = time.time()
            self._running_process.communicate()  # drain pipes
            return True
        return False

    # Returns the return code for the test, or None if it is still running.
    def return_code(self):
        return self._running_process.returncode

    # Returns the original command line for the test.
    def command_line(self):
        return str(self._command_line)

    # Returns the runtime for the test.
    #
    # If the test is currently running, this returns currently elapsed
    # time. Otherwise it returns the runtime from start to end of the
    # wrapped test.
    def runtime(self):
        return (self._end_time - self._start_time if self._end_time is not None
                else time.time() - self._start_time)

    # Force the test to terminate if it is currently running.
    def terminate(self):
        self._running_process.terminate()


# Start an individual Test.
#
# Returns a StartedTest if the test could be started, and None otherwise.
def start_test(test_object, parallel=None, timeout=None):
    if test_object.test_type == 'v1':
        command_line = ['fx', 'shell', 'run-test-component']
        if timeout:
            command_line.append(f'--timeout={int(timeout)}')
        command_line.append(test_object.package_url)
        return StartedTest.create(command_line)
    if test_object.test_type == 'v2':
        command_line = [
            'fx', 'shell', 'run-test-suite', test_object.package_url
        ]
        if parallel:
            command_line.append('--parallel')
            command_line.append(f'{parallel}')
        if timeout:
            command_line.append('--timeout')
            command_line.append(f'{int(timeout)}')
        return StartedTest.create(command_line)
    else:
        return None


# Implementation for --run mode.
def run_tests(to_run,
              timeout_seconds=None,
              max_running_tests=None,
              parallel=None):

    test_iter = iter(to_run)
    running_tests = []
    outcomes = defaultdict(list)
    start_time = time.time()

    # Internal function to poll and update individual tests.
    def process_running_test(test):
        if test.poll_done():
            mode = ''
            if test.return_code() == 0:
                mode = 'SUCCESS'
            else:
                mode = f'code {test.return_code()}'
            outcomes[mode].append(test)
            print(f'{mode} [{test.runtime()}]: {test.command_line()}')
            return False
        elif test.runtime() > timeout_seconds:
            test.terminate()

        return True

    # Internal function to print status of the run.
    def print_status():
        skipped = len(outcomes['SKIPPED']) if 'SKIPPED' in outcomes else 0
        total_done = sum([len(v) for v in outcomes.values()]) - skipped
        print(
            f'Status: {total_done}/{len(to_run)} {len(running_tests)} running {skipped} skipped'
        )
        for (k, v) in sorted(outcomes.items()):
            if k == 'SKIPPED':
                continue
            runtimes = list(
                map(lambda x: x.runtime() if hasattr(x, 'runtime') else 0, v))
            total_time = sum(runtimes)
            average_time = total_time / len(runtimes)
            print(
                f'  {k:10s}: {len(runtimes):7d} {total_time:9.3f} {average_time:9.3f} avg.'
            )
        overall_time = time.time() - start_time
        avg_overall_time = overall_time / total_done if total_done != 0 else 0
        print(
            f'  TOTAL     : {total_done:7d} {overall_time:9.3f} {avg_overall_time:9.3f} avg.'
        )

    next_test = next(test_iter, None)

    while running_tests or next_test is not None:
        start_set = {t.command_line() for t in running_tests}

        running_tests = list(filter(process_running_test, running_tests))

        # Continueally start tests so long as there is a test to be
        # started and we have not yet reached the maximum.
        while len(running_tests) < max_running_tests and next_test is not None:
            started_test = start_test(next_test[0], parallel, timeout_seconds)

            if started_test is not None:
                print(f'Started {next_test[0].key()}')
                running_tests.append(started_test)
            else:
                outcomes['SKIPPED'].append(next_test)
                print(f'Skipped {next_test[0].key()}')

            next_test = next(test_iter, None)

        # Print output if we have changed the set of running tests since the start of this iteration.
        if {t.command_line() for t in running_tests} != start_set:
            print_status()

        # Sleep for 10ms before continuing loop to reduce CPU load.
        time.sleep(.01)

    print_status()


if __name__ == '__main__':
    sys.exit(main())
