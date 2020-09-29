// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

List<void Function()> _tests = [];

const int perftestProcessRuns = 6;

String tmpPerfResultsJson(String benchmarkBinary) {
  return '/tmp/perf_results_$benchmarkBinary.json';
}

void runFidlBenchmark(String benchmarkBinary, String args) {
  final resultsFile = tmpPerfResultsJson(benchmarkBinary);
  final path = '/bin/$benchmarkBinary $args';
  _tests.add(() {
    test(benchmarkBinary, () async {
      final helper = await PerfTestHelper.make();
      final result = await helper.sl4fDriver.ssh.run(path);
      expect(result.exitCode, equals(0));
      await helper.processResults(resultsFile);
    }, timeout: Timeout.none);
  });
}

// Runs a benchmark that uses the C++ perftest runner.
// It is believed that benchmarks converge to different means in different
// process runs (and reboots). Since each of these benchmarks are currently
// fast to run (a few secs), run the binary several times for more stability
// in perfcompare results.
// However, this is currently not possible with catapult_converter, so only
// report the first result to catapult.
void runPerftestFidlBenchmark(String benchmarkBinary) {
  final resultsFile = tmpPerfResultsJson(benchmarkBinary);
  _tests.add(() {
    test(benchmarkBinary, () async {
      final helper = await PerfTestHelper.make();
      final result = await helper.sl4fDriver.ssh
          .run('/bin/$benchmarkBinary -p --quiet --out $resultsFile');
      expect(result.exitCode, equals(0));
      // This makes the results visible to both perfcompare and Catapult.
      await helper.processResults(resultsFile);

      for (var process = 0; process < perftestProcessRuns - 1; ++process) {
        final result = await helper.sl4fDriver.ssh
            .run('/bin/$benchmarkBinary -p --quiet --out $resultsFile');
        expect(result.exitCode, equals(0));
        // This makes the results visible to perfcompare but not Catapult.
        await helper.storage.dumpFile(resultsFile,
            'results_fidl_microbenchmarks_process$process', 'fuchsiaperf.json');
      }
    }, timeout: Timeout.none);
  });
}

void main(List<String> args) {
  enableLoggingOutput();

  runFidlBenchmark('go_fidl_microbenchmarks',
      '--out_file ${tmpPerfResultsJson('go_fidl_microbenchmarks')}');
  runPerftestFidlBenchmark('hlcpp_fidl_microbenchmarks');
  runPerftestFidlBenchmark('lib_fidl_microbenchmarks');
  runPerftestFidlBenchmark('llcpp_fidl_microbenchmarks');
  runPerftestFidlBenchmark('walker_fidl_microbenchmarks');
  runFidlBenchmark('rust_fidl_microbenchmarks',
      tmpPerfResultsJson('rust_fidl_microbenchmarks'));
  runPerftestFidlBenchmark('reference_fidl_microbenchmarks');

  _tests.add(() {
    test('dart_fidl_microbenchmarks', () async {
      final helper = await PerfTestHelper.make();
      const resultsFile =
          '/data/r/sys/r/fidl_benchmarks/fuchsia.com:dart_fidl_benchmarks:0#meta:dart_fidl_benchmarks.cmx/results.json';
      const command = 'run-test-component --realm-label=fidl_benchmarks '
          'fuchsia-pkg://fuchsia.com/dart_fidl_benchmarks#meta/dart_fidl_benchmarks.cmx';
      final result = await helper.sl4fDriver.ssh.run(command);
      expect(result.exitCode, equals(0));
      await helper.processResults(resultsFile);
    }, timeout: Timeout.none);
  });

  runShardTests(args, _tests);
}
