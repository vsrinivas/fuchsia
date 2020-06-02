// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/sl4f.dart';
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';
import 'package:logging/logging.dart';

import 'helpers.dart';

const String _catapultConverterPath = 'runtime_deps/catapult_converter';
const String _trace2jsonPath = 'runtime_deps/trace2json';

final _log = Logger('TouchInputLatencyMetricsProcessor');

// Custom MetricsProcessor for this test that relies on trace events tagged with the
// "touch-input-test" category.
List<TestCaseResults> touchInputLatencyMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final inputLatency = getArgValuesFromEvents<num>(
          filterEventsTyped<InstantEvent>(getAllEvents(model),
              category: 'touch-input-test', name: 'input_latency'),
          'elapsed_time')
      .map((t) => t.toDouble())
      .toList();

  if (inputLatency.length != 1) {
    throw ArgumentError("touch-input-test didn't log an elapsed time.");
  }

  _log.info('Elapsed time: ${inputLatency.first} ns.');

  final List<TestCaseResults> testCaseResults = [
    TestCaseResults('touch_input_latency', Unit.nanoseconds, inputLatency)
  ];

  return testCaseResults;
}

const _touchInputLatencyMetricsRegistry = {
  'touch_input_latency': touchInputLatencyMetricsProcessor,
};

void _addTest(String testName) {
  test(testName, () async {
    final helper = await PerfTestHelper.make();

    // Start tracing.
    final traceSession = await helper.performance
        .initializeTracing(categories: ['touch-input-test']);
    await traceSession.start();

    await helper.sl4fDriver.ssh.run(
        '/bin/run -d fuchsia-pkg://fuchsia.com/touch-input-test#meta/touch-input-test.cmx');

    await traceSession.stop();

    final fxtTraceFile = await traceSession.terminateAndDownload(testName);
    final jsonTraceFile = await helper.performance
        .convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    final metricsSpecSet = MetricsSpecSet(
      testName: testName,
      metricsSpecs: [
        MetricsSpec(name: 'touch_input_latency'),
      ],
    );

    expect(
        await helper.performance.processTrace(metricsSpecSet, jsonTraceFile,
            converterPath: _catapultConverterPath,
            registry: _touchInputLatencyMetricsRegistry),
        isNotNull);
  });
}

void main() {
  enableLoggingOutput();

  _addTest('fuchsia.input_latency.one-flutter');
}
