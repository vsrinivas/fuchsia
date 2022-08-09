// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io' show File;
import 'dart:convert';

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  // We run the fuchsia_microbenchmarks process multiple times.  That is
  // useful for tests that exhibit between-process variation in results
  // (e.g. due to memory layout chosen when a process starts) -- it
  // reduces the variation in the average that we report.
  const int processRuns = 6;

  // We override the default number of within-process iterations of
  // each test case and use a lower value.  This reduces the overall
  // time taken and reduces the chance that these invocations hit
  // Infra Swarming tasks' IO timeout (swarming_io_timeout_secs --
  // the amount of time that a task is allowed to run without
  // producing log output).
  const int iterationsPerTestPerProcess = 120;

  // For running with tracing enabled we run the following tests:
  //
  // - The Tracing suite, which creates exercises the trace-engine code via
  //   TRACE macros
  // - Syscall/Null and Syscall/ManyArgs which exercise ktrace_tiny records
  // - Channel/WriteRead which exercises regular ktrace records
  const filterRegex =
      "'(^Tracing/)|(^Syscall/Null\$)|(^Syscall/ManyArgs\$)|(^Channel/WriteRead/1024bytes/1handles\$)'";

  test('fuchsia_microbenchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'fuchsia_microbenchmarks_perftestmode',
        componentName: 'fuchsia_microbenchmarks_perftestmode.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}'
            ' --runs $iterationsPerTestPerProcess',
        processRuns: processRuns,
        expectedMetricNamesFile: 'fuchsia.microbenchmarks.txt');
  }, timeout: Timeout.none);

  // Run some of the microbenchmarks with tracing enabled to measure the
  // overhead of tracing.
  test('fuchsia_microbenchmarks_tracing_categories_enabled', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results_tracing.json';

    final List<File> resultsFiles = [];
    for (var process = 0; process < processRuns; ++process) {
      final result = await helper.sl4fDriver.ssh
          .run('/bin/trace record --spawn=true --buffering-mode=circular'
              ' --categories=kernel,benchmark /bin/fuchsia_microbenchmarks -p'
              ' --quiet --out $resultsFile --runs $iterationsPerTestPerProcess'
              ' --filter $filterRegex --enable-tracing');
      expect(result.exitCode, equals(0));
      // The json file fuchsia_microbenchmarks outputs will have the same suite
      // and test names as the non perf ones. Here, we rewrite the suite names
      // in the json file so catapult can distinguish them.
      var resultsJson =
          utf8.decoder.convert(await helper.storage.readFile(resultsFile));
      var results = jsonDecode(resultsJson);

      for (var testResult in results) {
        var suiteName = testResult['test_suite'];
        testResult['test_suite'] = suiteName + '.tracing';
      }

      var localResults = await helper.dump.writeAsString(
          'results_microbenchmarks_tracing_process$process',
          'fuchsiaperf_full.json',
          jsonEncode(results));
      resultsFiles.add(localResults);
    }
    await helper.processResultsSummarized(resultsFiles,
        expectedMetricNamesFile: 'fuchsia.microbenchmarks.tracing.txt');
  }, timeout: Timeout.none);

  // Run some of the microbenchmarks with tracing enabled but each category
  // disabled to measure the overhead of a trace event with the category turned
  // off.
  test('fuchsia_microbenchmarks_tracing_categories_disabled', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results_tracing_categories_disabled.json';

    final List<File> resultsFiles = [];
    for (var process = 0; process < processRuns; ++process) {
      final result = await helper.sl4fDriver.ssh
          .run('/bin/trace record --spawn=true --buffering-mode=circular'
              ' --categories=none /bin/fuchsia_microbenchmarks -p'
              ' --quiet --out $resultsFile --runs $iterationsPerTestPerProcess'
              ' --filter $filterRegex --enable-tracing');
      expect(result.exitCode, equals(0));
      // The json file fuchsia_microbenchmarks outputs will have the same suite
      // and test names as the non perf ones. Here, we rewrite the suite names
      // in the json file so catapult can distinguish them.
      var resultsJson =
          utf8.decoder.convert(await helper.storage.readFile(resultsFile));
      var results = jsonDecode(resultsJson);

      for (var testResult in results) {
        var suiteName = testResult['test_suite'];
        testResult['test_suite'] = suiteName + '.tracing_categories_disabled';
      }

      var localResults = await helper.dump.writeAsString(
          'results_microbenchmarks_tracing_category_disabled_process$process',
          'fuchsiaperf_full.json',
          jsonEncode(results));
      resultsFiles.add(localResults);
    }
    await helper.processResultsSummarized(resultsFiles,
        expectedMetricNamesFile:
            'fuchsia.microbenchmarks.tracing_categories_disabled.txt');
  }, timeout: Timeout.none);
}
