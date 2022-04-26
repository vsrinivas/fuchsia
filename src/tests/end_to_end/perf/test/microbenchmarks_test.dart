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

  test('fuchsia_microbenchmarks', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results.json';

    final List<File> resultsFiles = [];
    for (var process = 0; process < processRuns; ++process) {
      final result = await helper.sl4fDriver.ssh
          .run('/bin/fuchsia_microbenchmarks -p --quiet --out $resultsFile'
              ' --runs $iterationsPerTestPerProcess');
      expect(result.exitCode, equals(0));
      resultsFiles.add(await helper.storage.dumpFile(resultsFile,
          'results_microbenchmarks_process$process', 'fuchsiaperf_full.json'));
    }
    await helper.processResultsSummarized(resultsFiles);
  }, timeout: Timeout.none);

  // Run some of the microbenchmarks with tracing enabled to measure the
  // overhead of tracing.
  test('fuchsia_microbenchmarks_tracing', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results_tracing.json';

    final List<File> resultsFiles = [];
    for (var process = 0; process < processRuns; ++process) {
      final result = await helper.sl4fDriver.ssh
          .run('/bin/trace record --spawn=true --buffering-mode=circular'
              ' --categories=kernel /bin/fuchsia_microbenchmarks -p'
              ' --quiet --out $resultsFile  --runs $iterationsPerTestPerProcess'
              ' --filter ^Syscall');
      expect(result.exitCode, equals(0));
      // The json file fuchsia_microbenchmarks outputs will have the same suite
      // and test names as the non perf ones. Here, we rewrite the suite names
      // in the json file to append '.tracing' so catapult can distinguish them.
      var resultsJson =
          utf8.decoder.convert(await helper.storage.readFile(resultsFile));
      var results = jsonDecode(resultsJson);

      for (var testResult in results) {
        var suiteName = testResult['test_suite'];
        testResult['test_suite'] = suiteName + '.tracing';
      }
      var localResults = File(
          'results_microbenchmarks_tracing_process$process.fuchsiaperf_full.json');
      var output = localResults.openWrite()..write(jsonEncode(results));
      await output.flush();
      await output.close();
      resultsFiles.add(localResults);
    }
    await helper.processResultsSummarized(resultsFiles);
  }, timeout: Timeout.none);
}
