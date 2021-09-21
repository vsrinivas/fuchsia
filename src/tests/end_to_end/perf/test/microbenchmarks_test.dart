// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io' show File;

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('fuchsia_microbenchmarks', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results.json';

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
}
