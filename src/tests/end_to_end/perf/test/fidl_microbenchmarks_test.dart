// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

String tmpPerfResultsJson(String benchmarkBinary) {
  return '/tmp/perf_results_$benchmarkBinary.json';
}

void runFidlBenchmark(String benchmarkBinary, [String optArgs]) {
  final resultsFile = tmpPerfResultsJson(benchmarkBinary);
  final path = (optArgs == null)
      ? '/bin/$benchmarkBinary -p --quiet --out $resultsFile'
      : '/bin/$benchmarkBinary $optArgs';
  test(benchmarkBinary, () async {
    final helper = await PerfTestHelper.make();
    final result = await helper.sl4fDriver.ssh.run(path);
    expect(result.exitCode, equals(0));
    await helper.processResults(resultsFile);
  }, timeout: Timeout.none);
}

void main() {
  enableLoggingOutput();

  runFidlBenchmark('go_fidl_microbenchmarks',
      '--out_file ${tmpPerfResultsJson('go_fidl_microbenchmarks')}');
  runFidlBenchmark('hlcpp_fidl_microbenchmarks');
  runFidlBenchmark('lib_fidl_microbenchmarks');
  runFidlBenchmark('llcpp_fidl_microbenchmarks');
  runFidlBenchmark('roundtrip_fidl_benchmarks',
      tmpPerfResultsJson('roundtrip_fidl_benchmarks'));
  runFidlBenchmark('rust_fidl_microbenchmarks',
      tmpPerfResultsJson('rust_fidl_microbenchmarks'));
}
