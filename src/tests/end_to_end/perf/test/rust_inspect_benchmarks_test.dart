// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const _testName = 'fuchsia.rust_inspect.benchmarks';
const _appPath =
    'fuchsia-pkg://fuchsia.com/rust_inspect_benchmarks#meta/rust_inspect_benchmarks.cmx';
const _catapultConverterPath = 'runtime_deps/catapult_converter';
const _trace2jsonPath = 'runtime_deps/trace2json';

/// Custom MetricsProcessor that generates one TestCaseResults for each unique
/// event name in the model with category 'benchmark'.
///
/// This avoids creating a MetricsSpec for each event separately.
List<TestCaseResults> _rustInspectBenchmarksMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final allEvents = filterEventsTyped<DurationEvent>(getAllEvents(model),
      category: 'benchmark');

  final Map<String, TestCaseResults> results = {};

  for (final event in allEvents) {
    results[event.name] ??= TestCaseResults(
      // Convert event name a::b or a::b/c to test name a/b or a/b/c
      event.name.replaceFirst('::', '/'),
      Unit.milliseconds,
      [],
    );
    results[event.name].values.add(event.duration.toMillisecondsF());
  }

  return results.values.toList();
}

void main() {
  enableLoggingOutput();

  test(_testName, () async {
    final helper = await PerfTestHelper.make();

    final traceSession =
        await helper.performance.initializeTracing(categories: ['benchmark']);
    await traceSession.start();

    // TODO(fxbug.dev/53552): Use launch facade instead of SSH.
    final benchmarkProcess = await helper.sl4fDriver.ssh
        .start('/bin/run $_appPath --iterations 50000 --benchmark writer');
    // 50000 iterations to ensure the benchmark runs longer than 30 seconds.

    sleep(Duration(seconds: 30));
    benchmarkProcess.kill();

    await traceSession.stop();

    final fxtTraceFile = await traceSession.terminateAndDownload(_testName);
    final jsonTraceFile = await helper.performance
        .convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    await helper.performance.processTrace(
      MetricsSpecSet(
        metricsSpecs: [MetricsSpec(name: 'rust_inspect')],
        testName: _testName,
      ),
      jsonTraceFile,
      converterPath: _catapultConverterPath,
      registry: {
        'rust_inspect': _rustInspectBenchmarksMetricsProcessor,
      },
    );
  }, timeout: Timeout.none);
}
