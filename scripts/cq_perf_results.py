#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""cq_perf_results.py prints the performance testing results of a CL.

From a change ID (the last number in
https://fuchsia-review.googlesource.com/c/fuchsia/+/255116), cq_perf_results.py
retrieves the last CQ run and compares the its performance test results with
the performance test result of its parent, as found in the CI queue.
"""

import argparse
import json
import numpy
import operator
import os
import scipy.stats
import urllib.request

# URL template for listing all tests on a given build.
_TEST_LIST_TMPL = 'https://logs.chromium.org/logs/fuchsia/buildbucket/cr-buildbucket.appspot.com/%s/+/steps/all_test_results/0/logs/summary.json/0?format=raw'

# URL template for retrieving the output of a passing test on a given build.
_TEST_OUTPUT = 'https://logs.chromium.org/logs/fuchsia/buildbucket/cr-buildbucket.appspot.com/%s/+/steps/all_test_results/0/steps/all_passed_tests/0/steps/%s/0/logs/stdio/0?format=raw'

# URL template to retrieve data about a change.
_CHANGE_URL_TMPL = 'https://fuchsia-review.googlesource.com/changes/%s?o=CURRENT_REVISION&o=CURRENT_COMMIT'

# URL template to retrieve the CQ builds for a given change patchset.
_BUILD_CQ_URL_TMPL = 'https://cr-buildbucket.appspot.com/_ah/api/buildbucket/v1/search?max_builds=500&fields=builds%%28bucket%%2Cfailure_reason%%2Cid%%2Cparameters_json%%2Cresult%%2Cstatus%%2Ctags%%2Curl%%29&tag=buildset%%3Apatch%%2Fgerrit%%2Ffuchsia-review.googlesource.com%%2F%s%%2F%d'

# URL template to retrieve the CI builds for a given committed change.
_BUILD_CI_URL_TMPL = 'https://cr-buildbucket.appspot.com/_ah/api/buildbucket/v1/search?max_builds=500&fields=builds%%28bucket%%2Cfailure_reason%%2Cid%%2Cparameters_json%%2Cresult%%2Cstatus%%2Ctags%%2Curl%%29&tag=buildset%%3Acommit%%2Fgit%%2F%s'

# Default botname for performance tests.
_BOTNAME = 'peridot-x64-perf-dawson_canyon'

# Get the name of all non-catapult-upload tests on a given build.
def _get_test_names(build_id):
  test_list_url = _TEST_LIST_TMPL % build_id
  test_list_request = urllib.request.urlopen(test_list_url)
  test_list = json.loads(test_list_request.read().decode('utf-8'))
  test_names = tuple(entry['name'] for entry in test_list['tests']
    if not entry['name'].endswith('.catapult_json'))
  return test_names

# Get a specific test result from a build.
def _get_perf_test_results(build_id, test_name):
  test_result_url = _TEST_OUTPUT % (build_id, test_name)
  test_result_request = urllib.request.urlopen(test_result_url)
  test_result = json.loads(test_result_request.read().decode('utf-8'))
  results = []
  for test in test_result:
    name = '%s/%s' % (test['test_suite'], test['label'])
    values = test['values']
    results.append((name, values))
  return results

# Get all test results for a given build ID.
def _get_results_for_build(build_id):
  test_names = _get_test_names(build_id)
  results = {}
  for test_name in test_names:
    result = _get_perf_test_results(build_id, test_name)
    for label, value in result:
      results[label] = value
  return results

# Get the latest build ID for a given gerrit change ID.
def _get_build_from_review(change_id, botname):
  change_url = _CHANGE_URL_TMPL % change_id
  change_request = urllib.request.urlopen(change_url)
  change = json.loads(change_request.read().decode('utf-8')[5:])
  if change['status'] == 'MERGED':
    commit_id = change['current_revision']
    build = _get_ci_build(commit_id, botname)
  else:
    patchset = change['revisions'][change['current_revision']]['_number']
    build = _get_cq_build(change_id, patchset, botname)
  parent = change['revisions'][change['current_revision']]['commit']['parents'][0]['commit']

  return build, parent,

# Get the build ID on the CQ for a given gerrit change ID and patchset.
def _get_cq_build(change_id, patchset, botname):
  build_url = _BUILD_CQ_URL_TMPL % (change_id, patchset)
  build_request = urllib.request.urlopen(build_url).read().decode('utf-8')
  builds = json.loads(build_request)
  target_tag = 'builder:' + botname
  for build in builds['builds']:
    if target_tag in build['tags']:
      return build['id']
  raise KeyError("Unable to find the target builder")

# Get the build ID on the CI for a given commit ID.
def _get_ci_build(commit_id, botname):
  build_url = _BUILD_CI_URL_TMPL % commit_id
  build_request = urllib.request.urlopen(build_url)
  builds = json.loads(build_request.read().decode('utf-8'))
  target_tag = 'builder:' + botname
  for build in builds['builds']:
    if target_tag in build['tags']:
      return build['id']
  raise KeyError("Unable to find the target builder")

def _compute_output_format_strings(target_build):
  # We want to align test names and results, so we compute the maximum size
  # needed from the test name length.
  max_test_name_length = max(len(test_name) for test_name in target_build)
  both_tests_format_string = '{:' + str(max_test_name_length) + \
    's}: {:8.4f} -> {:8.4f}  {:6.2f} % variation, {:5.3f} p-value'
  single_test_format_string = '{:' + str(max_test_name_length) + \
        's}: {:8.4f} (no corresponding test in base commit)'
  no_data_format_string = '{:' + str(max_test_name_length) + \
    's}: {:8.4f} -> {:8.4f}  {:6.2f} % variation, not enough data'
  return (both_tests_format_string, single_test_format_string,
    no_data_format_string, )

def main():
  description="""A tool to detect performance test changes on changes.

From a change ID (series of multiple digits, and last part of the gerrit URL:
https://fuchsia-review.googlesource.com/c/fuchsia/+/CHANGE_ID), this tool
retrieves the last CQ run and compares the its performance test results with
the performance test result of its parent, as found in the CI queue.

You will want to use the --base_build argument if you are doing chained
changes. This script assumes the usual gerrit workflow where conflicting
changes are rebased instead of merged, so each commit has at most one parent."""
  epilog = """Example:
$> ./cq_perf_results.py --botname peridot-x64-perf-dawson_canyon 255116
  """
  argument_parser = argparse.ArgumentParser(description=description,
    epilog=epilog, formatter_class=argparse.RawDescriptionHelpFormatter)
  argument_parser.add_argument('change_id', default=None,
    help="Change ID from Gerrit")
  argument_parser.add_argument('--botname', default=_BOTNAME,
    help="Name of the bot running the performance tests. Default: " + _BOTNAME)
  argument_parser.add_argument('--base_build', default=None,
    help="Base build to use (default: use the CI build of the base commit)")
  args = argument_parser.parse_args()

  # We first get the build ID for the base change, then get its perf test
  # results.
  target_build_id, parent_id = _get_build_from_review(args.change_id,
    args.botname)
  print('Target build id', target_build_id, 'parent id', parent_id)
  target_build = _get_results_for_build(target_build_id)

  # Get the base build ID and get its perf test results.
  if not args.base_build:
    base_build_id = _get_ci_build(parent_id, args.botname)
  else:
    base_build_id = args.base_build
  print('Base build id', base_build_id)
  base_build = _get_results_for_build(base_build_id)

  both_tests_format_string, single_test_format_string, no_data_format_string = \
      _compute_output_format_strings(target_build)
  for test_name, value in sorted(target_build.items(),
      key=operator.itemgetter(0)):
    if test_name not in base_build:
      print(single_test_format_string.format(
          test_name, numpy.mean(value)))
      continue
    if len(value) == 1:
      print(no_data_format_string.format(test_name,
        base_build[test_name][0],
        value[0],
        (value[0]-base_build[test_name][0])*100.0/base_build[test_name][0]))
      continue
    base_mean = numpy.mean(base_build[test_name][1:])
    target_mean = numpy.mean(value[1:])
    # We use a 2-sample Kolmogorov-Smirnov statistic, for the following reasons:
    # - It is non-parametric;
    # - It does not assume normality of samples.
    _, pvalue = scipy.stats.ks_2samp(base_build[test_name][1:], value[1:])
    print(both_tests_format_string.format(test_name,
      base_mean,
      target_mean,
      (target_mean-base_mean)*100.0/base_mean,
      pvalue))

if __name__ == '__main__':
  main()
