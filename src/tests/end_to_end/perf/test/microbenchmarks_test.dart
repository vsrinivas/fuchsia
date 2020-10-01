// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
    //
    // The perfcompare.py tool is able to use results from multiple process
    // runs, and this helps reduce the widths of confidence intervals it
    // reports for the microbenchmarks.
    //
    // However, catapult_converter does not yet support merging results
    // from multiple process runs, so it only uses the results from the
    // first process run.  (That limitation is partly because
    // catapult_converter is run separately on the results from each
    // process run.)
    const int processRuns = 6;

    // Pass "--runs" to reduce the number of within-process iterations of
    // each test case.  This reduces the overall time taken and reduces the
    // chance that this invocation hits Infra Swarming tasks' IO timeout
    // (swarming_io_timeout_secs -- the amount of time that a task is
    // allowed to run without producing log output).
    final result = await helper.sl4fDriver.ssh
        .run('/bin/fuchsia_microbenchmarks -p --quiet --runs 400'
            ' --out $resultsFile');
    expect(result.exitCode, equals(0));
    // This makes the results visible to both perfcompare and Catapult.
    await helper.processResults(resultsFile);

    for (var process = 0; process < processRuns - 1; ++process) {
      // Pass "--runs" to reduce the number of within-process iterations of
      // each test case.  This reduces the cost of these extra process
      // runs.
      final result = await helper.sl4fDriver.ssh
          .run('/bin/fuchsia_microbenchmarks -p --quiet --runs 80'
              ' --out $resultsFile');
      expect(result.exitCode, equals(0));
      // This makes the results visible to perfcompare but not Catapult.
      await helper.storage.dumpFile(resultsFile,
          'results_microbenchmarks_process$process', 'fuchsiaperf.json');
    }
  }, timeout: Timeout.none);
}
