// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

String tmpPerfResultsJson(String benchmarkBinary) {
  return '/tmp/perf_results_$benchmarkBinary.json';
}

void runBenchmark(String benchmarkBinary, String resultsFile) {
  final command = '/bin/$benchmarkBinary $resultsFile';
  test(benchmarkBinary, () async {
    final helper = await PerfTestHelper.make();
    var result = await helper.sl4fDriver.ssh.run(command);
    expect(result.exitCode, equals(0));
    await helper.processResults(resultsFile);
    result = await helper.sl4fDriver.ssh.run('rm $resultsFile');
    expect(result.exitCode, equals(0));
  }, timeout: Timeout.none);
}

void main() {
  enableLoggingOutput();

  runBenchmark(
      'archivist_benchmarks', tmpPerfResultsJson('archivist_benchmarks'));
}
