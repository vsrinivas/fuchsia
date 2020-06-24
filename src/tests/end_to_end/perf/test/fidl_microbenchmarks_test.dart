// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:args/args.dart';
import 'package:test/test.dart';

import 'helpers.dart';

var _tests = [];

String tmpPerfResultsJson(String benchmarkBinary) {
  return '/tmp/perf_results_$benchmarkBinary.json';
}

void runFidlBenchmark(String benchmarkBinary, [String optArgs]) {
  final resultsFile = tmpPerfResultsJson(benchmarkBinary);
  final path = (optArgs == null)
      ? '/bin/$benchmarkBinary -p --quiet --out $resultsFile'
      : '/bin/$benchmarkBinary $optArgs';
  _tests.add(() {
    test(benchmarkBinary, () async {
      final helper = await PerfTestHelper.make();
      final result = await helper.sl4fDriver.ssh.run(path);
      expect(result.exitCode, equals(0));
      await helper.processResults(resultsFile);
    }, timeout: Timeout.none);
  });
}

void main(List<String> args) {
  enableLoggingOutput();

  runFidlBenchmark('go_fidl_microbenchmarks',
      '--encode_counts --out_file ${tmpPerfResultsJson('go_fidl_microbenchmarks')}');
  runFidlBenchmark('hlcpp_fidl_microbenchmarks');
  runFidlBenchmark('lib_fidl_microbenchmarks');
  runFidlBenchmark('llcpp_fidl_microbenchmarks');
  runFidlBenchmark('rust_fidl_microbenchmarks',
      tmpPerfResultsJson('rust_fidl_microbenchmarks'));
  runFidlBenchmark('walker_fidl_microbenchmarks');

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

  // The Dart test library is supposed to support sharding, but its
  // sharding options do not seem to be accessible when running Dart tests
  // on Fuchsia, so we reimplement the same options here.
  final parser = ArgParser()
    ..addOption('total-shards',
        help: 'Number of total shards to split test suites into.',
        defaultsTo: '1')
    ..addOption('shard-index',
        help: 'Which shard of test suites to run.', defaultsTo: '0');
  final argResults = parser.parse(args);

  int totalShards = int.parse(argResults['total-shards']);
  int shardIndex = int.parse(argResults['shard-index']);
  for (var i = 0; i < _tests.length; i++) {
    if (i % totalShards == shardIndex) {
      _tests[i]();
    }
  }
}
