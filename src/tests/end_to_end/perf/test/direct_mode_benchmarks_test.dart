// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

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

  // For running with direct mode enabled we run the following tests:
  const filterRegex = "'^(Channel/WriteRead|Stream|Syscall|Vmar)/.*'";

  test('direct_mode_microbenchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'direct_mode_microbenchmarks',
        componentName: 'direct_mode_microbenchmarks.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}'
            ' --runs $iterationsPerTestPerProcess --filter $filterRegex',
        processRuns: processRuns,
        expectedMetricNamesFile: 'fuchsia.microbenchmarks.direct_mode.txt');
  }, timeout: Timeout.none);
}
