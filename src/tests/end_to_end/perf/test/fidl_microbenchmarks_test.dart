// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io' show File;

import 'package:test/test.dart';

import 'helpers.dart';

List<void Function()> _tests = [];

const int perftestProcessRuns = 6;

String tmpPerfResultsJson(String benchmarkBinary) {
  return '/tmp/perf_results_$benchmarkBinary.json';
}

// Runs a benchmark that uses the C++ perftest runner.
// It is believed that benchmarks converge to different means in different
// process runs (and reboots). Since each of these benchmarks are currently
// fast to run (a few secs), run the binary several times for more stability.
void runPerftestFidlBenchmark(
    String benchmarkBinary, String expectedMetricNamesFile) {
  final resultsFile = tmpPerfResultsJson(benchmarkBinary);
  _tests.add(() {
    test(benchmarkBinary, () async {
      final helper = await PerfTestHelper.make();

      final List<File> resultsFiles = [];
      for (var process = 0; process < perftestProcessRuns; ++process) {
        final result = await helper.sl4fDriver.ssh
            .run('/bin/$benchmarkBinary -p --quiet --out $resultsFile');
        expect(result.exitCode, equals(0));
        resultsFiles.add(await helper.storage.dumpFile(
            resultsFile,
            'results_fidl_microbenchmarks_process$process',
            'fuchsiaperf_full.json'));
      }
      await helper.processResultsSummarized(resultsFiles,
          expectedMetricNamesFile: expectedMetricNamesFile);
    }, timeout: Timeout.none);
  });
}

void main(List<String> args) {
  enableLoggingOutput();

  runPerftestFidlBenchmark(
      'cpp_fidl_microbenchmarks', 'fuchsia.fidl_microbenchmarks.cpp.txt');
  runPerftestFidlBenchmark(
      'hlcpp_fidl_microbenchmarks', 'fuchsia.fidl_microbenchmarks.hlcpp.txt');
  runPerftestFidlBenchmark(
      'lib_fidl_microbenchmarks', 'fuchsia.fidl_microbenchmarks.libfidl.txt');
  runPerftestFidlBenchmark(
      'llcpp_fidl_microbenchmarks', 'fuchsia.fidl_microbenchmarks.llcpp.txt');
  runPerftestFidlBenchmark('driver_cpp_fidl_microbenchmarks',
      'fuchsia.fidl_microbenchmarks.driver_cpp.txt');
  runPerftestFidlBenchmark('driver_llcpp_fidl_microbenchmarks',
      'fuchsia.fidl_microbenchmarks.driver_llcpp.txt');
  runPerftestFidlBenchmark(
      'walker_fidl_microbenchmarks', 'fuchsia.fidl_microbenchmarks.walker.txt');
  runPerftestFidlBenchmark('reference_fidl_microbenchmarks',
      'fuchsia.fidl_microbenchmarks.reference.txt');

  _tests
    ..add(() {
      test('go_fidl_microbenchmarks', () async {
        final helper = await PerfTestHelper.make();
        await helper.runTestCommand(
            (resultsFile) =>
                '/bin/go_fidl_microbenchmarks --out_file $resultsFile',
            expectedMetricNamesFile: 'fuchsia.fidl_microbenchmarks.go.txt');
      }, timeout: Timeout.none);
    })
    ..add(() {
      test('rust_fidl_microbenchmarks', () async {
        final helper = await PerfTestHelper.make();
        await helper.runTestCommand(
            (resultsFile) => '/bin/rust_fidl_microbenchmarks $resultsFile',
            expectedMetricNamesFile: 'fuchsia.fidl_microbenchmarks.rust.txt');
      }, timeout: Timeout.none);
    })
    ..add(() {
      test('dart_fidl_microbenchmarks', () async {
        final helper = await PerfTestHelper.make();
        await helper.runTestComponent(
            packageName: 'dart-fidl-benchmarks',
            componentName: 'dart-fidl-benchmarks.cm',
            commandArgs: '',
            expectedMetricNamesFile: 'fuchsia.fidl_microbenchmarks.dart.txt');
      }, timeout: Timeout.none);
    });

  runShardTests(args, _tests);
}
