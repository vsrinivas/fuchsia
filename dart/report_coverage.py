#!/usr/bin/env python

# This program generates a combined coverage report for all host-side dart tests.
# See example_commands and arg help strings in ParseArgs() for usage.
#
# Implementation sketch:
# Search the host_tests directory for tests that use dart-tools/fuchsia_tester.
# Run each test with --coverage and --coverage-path.
# Combine the coverage data from each test into one.
# Generate an HTML report.
#
# This is all pretty hacky. Longer term efforts to make this more automatic and
# less hacky tracked by IN-427.

from __future__ import print_function
import argparse
import collections
import distutils.spawn
import glob
import os
from multiprocessing.pool import ThreadPool
import paths
import re
import subprocess
import sys
import tempfile


TestResult = collections.namedtuple(
    'TestResult', ('exit_code', 'coverage_data_path', 'package_dir'))
DEV_NULL = open('/dev/null', 'w')
# TODO(https://github.com/flutter/flutter/issues/19486): Remove this once fixed
# and fix rolled.
TEST_BLACKLIST = {'dartfmt_extras_tests'}
LCOV = 'lcov'
GENHTML = 'genhtml'

def ParseArgs():
  example_commands = """

  Examples:
  $ report_coverage.py --report-dir /tmp/cov
  $ report_coverage.py --test-patterns 'foo_*_test,bar_test' --report-dir ...
  $ report_coverage.py --out-dir out/x64 --report-dir ...
  """
  p = argparse.ArgumentParser(
      description='Generates a coverage report for dart tests',
      epilog=example_commands,
      formatter_class=argparse.RawDescriptionHelpFormatter)

  p.add_argument(
      '--report-dir',
      type=str,
      help='Where to write the report. Will be created if needed',
      required=True)
  p.add_argument(
      '--test-patterns',
      type=str,
      help=('Comma-separated list of glob patterns to match against test file '
            'base names'),
      default='*')
  p.add_argument('--out-dir', type=str, help='fuchsia build out dir')

  return p.parse_args()


def OutDir(args):
  if args.out_dir:
    out_dir = args.out_dir

    if not os.path.isabs(out_dir):
      out_dir = os.path.join(paths.FUCHSIA_ROOT, out_dir)

    if not os.path.isdir(out_dir):
      sys.exit(out_dir + ' is not a directory')
    return out_dir

  if os.environ.get('FUCHSIA_BUILD_DIR'):
    return os.environ.get('FUCHSIA_BUILD_DIR')

  fuchsia_dir = os.environ.get('FUCHSIA_DIR', paths.FUCHSIA_ROOT)
  fuchsia_config_file = os.path.join(fuchsia_dir, '.config')
  if os.path.isfile(fuchsia_config_file):
    fuchsia_config = open(fuchsia_config_file).read()
    m = re.search(r'FUCHSIA_BUILD_DIR=[\'"]([^\s\'"]*)', fuchsia_config)
    if m:
      return os.path.join(fuchsia_dir, m.group(1))

  return None


def RunTest(test_path):
  is_dart_test = False  # This is super hacky.
  test_directory = None
  test_lines = open(test_path, 'r').readlines()
  for test_line in test_lines:
    test_line_parts = test_line.strip().split()
    if not test_line_parts:
      continue
    if test_line_parts[0].endswith('dart-tools/fuchsia_tester'):
      is_dart_test = True
    elif test_line_parts[0].startswith('--test-directory='):
      test_directory = test_line_parts[0].split('=')[1]
  if not is_dart_test:
    return None
  if not test_directory:
    raise ValueError('Failed to find --test-directory arg in %s' % test_path)
  coverage_data_handle, coverage_data_path = tempfile.mkstemp()
  os.close(coverage_data_handle)
  exit_code = subprocess.call((
      test_path, '--coverage', '--coverage-path=%s' % coverage_data_path),
      stdout=DEV_NULL, stderr=DEV_NULL)
  if not os.stat(coverage_data_path).st_size:
    print('%s produced no coverage data' % os.path.basename(test_path),
          file=sys.stderr)
    return None
  return TestResult(
      exit_code, coverage_data_path, os.path.dirname(test_directory))


def MakeRelativePathsAbsolute(test_result):
  """Change source-file paths from relative-to-the-package to absolute."""
  with open(test_result.coverage_data_path, 'r+') as coverage_data_file:
    fixed_data = coverage_data_file.read().replace(
        'SF:', 'SF:%s/' % test_result.package_dir)
    coverage_data_file.seek(0)
    coverage_data_file.write(fixed_data)


def CombineCoverageData(test_results):
  output_handle, output_path = tempfile.mkstemp()
  os.close(output_handle)
  lcov_cmd = [LCOV, '--output-file', output_path]
  for test_result in test_results:
    lcov_cmd.extend(['--add-tracefile', test_result.coverage_data_path])
  subprocess.check_call(lcov_cmd, stdout=DEV_NULL, stderr=DEV_NULL)
  return output_path


def main():
  args = ParseArgs()
  out_dir = OutDir(args)
  if not out_dir:
    sys.exit('Couldn\'t find the output directory, pass --out-dir '
             '(absolute or relative to Fuchsia root) or set FUCHSIA_BUILD_DIR.')
  if not (distutils.spawn.find_executable(LCOV) and
          distutils.spawn.find_executable(GENHTML)):
    sys.exit('\'lcov\' and \'genhtml\' must be installed and in the PATH')
  host_tests_dir = os.path.join(out_dir, 'host_tests')
  test_patterns = args.test_patterns.split(',')
  test_paths = []
  for test_pattern in test_patterns:
    test_paths.extend(glob.glob(os.path.join(host_tests_dir, test_pattern)))
  test_paths = [tp for tp in test_paths
                if os.path.basename(tp) not in TEST_BLACKLIST]
  thread_pool = ThreadPool()
  results = thread_pool.map(RunTest, test_paths)
  results = [result for result in results if result] # filter None
  for result in results:
    if result.exit_code:
      sys.exit('%s failed' % test_path)
  thread_pool.map(MakeRelativePathsAbsolute, results)
  combined_coverage_path = CombineCoverageData(results)
  subprocess.check_call(
      (GENHTML, combined_coverage_path, '--output-directory', args.report_dir),
      stdout=DEV_NULL, stderr=DEV_NULL)
  print('Open file://%s to view the report' %
        os.path.join(os.path.abspath(args.report_dir), 'index.html'),
        file=sys.stderr)


if __name__ == '__main__':
    main()
