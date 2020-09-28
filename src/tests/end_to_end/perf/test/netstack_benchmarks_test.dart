// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/sl4f.dart';
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const _testName = 'fuchsia.netstack.udp_micro_benchmarks';
const _appPath =
    'fuchsia-pkg://fuchsia.com/netstack_benchmarks#meta/udp_benchmark.cmx';
const _catapultConverterPath = 'runtime_deps/catapult_converter';
const _trace2jsonPath = 'runtime_deps/trace2json';

// This mirrors kIterationCount in udp_benchmarks.cc.
const _expectedIterations = 1000;

List<TestCaseResults> _netstackBenchmarksMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final durations = filterEventsTyped<DurationEvent>(
    getAllEvents(model),
    category: 'benchmark',
    name: extraArgs['eventName'],
  ).map((event) => event.duration.toMillisecondsF()).toList();

  expect(durations.length, _expectedIterations);

  return [
    TestCaseResults(extraArgs['outputTestName'], Unit.milliseconds, durations)
  ];
}

void main() {
  enableLoggingOutput();

  test(_testName, () async {
    final helper = await PerfTestHelper.make();

    final traceSession =
        await helper.performance.initializeTracing(categories: ['benchmark']);
    await traceSession.start();

    // TODO(fxbug.dev/53552): Use launch facade instead of ssh.
    await helper.sl4fDriver.ssh.run('/bin/run $_appPath');

    await traceSession.stop();

    final fxtTraceFile = await traceSession.terminateAndDownload(_testName);
    final jsonTraceFile = await helper.performance
        .convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    final List<MetricsSpec> metricsSpecs = [];

    for (final mode in ['send', 'recv']) {
      for (final size in ['64', '1024', '2048', '4096']) {
        metricsSpecs.add(MetricsSpec(name: 'netstack', extraArgs: {
          'eventName': '${mode}_${size}bytes',
          'outputTestName': 'udp/$mode/${size}bytes',
        }));
      }
    }

    await helper.performance.processTrace(
      MetricsSpecSet(metricsSpecs: metricsSpecs, testName: _testName),
      jsonTraceFile,
      converterPath: _catapultConverterPath,
      registry: {'netstack': _netstackBenchmarksMetricsProcessor},
    );
  }, timeout: Timeout.none);
}
