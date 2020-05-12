// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/sl4f.dart';
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';
import 'package:logging/logging.dart';

import 'helpers.dart';

const String _catapultConverterPath = 'runtime_deps/catapult_converter';

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

const touchInputLatencyMetricsRegistry = {
  'touch_input_latency': touchInputLatencyMetricsProcessor,
};

Future<void> _killProcesses(PerfTestHelper helper) async {
  await helper.sl4fDriver.ssh.run('killall "root_presenter*"');
  await helper.sl4fDriver.ssh.run('killall "scenic*"');
  await helper.sl4fDriver.ssh.run('killall "basemgr*"');
  await helper.sl4fDriver.ssh.run('killall "flutter*"');
  await helper.sl4fDriver.ssh.run('killall "present_view*"');
  await helper.sl4fDriver.ssh.run('killall "one-flutter*"');
  await helper.sl4fDriver.ssh.run('killall "touch-input-test*"');
}

void _addTest(String testName) {
  test(testName, () async {
    final helper = await PerfTestHelper.make();

    await _killProcesses(helper);

    // Start tracing.
    final trace = helper.performance.trace(
        duration: Duration(seconds: 20),
        traceName: testName,
        categories: 'touch-input-test');

    final runApp = helper.sl4fDriver.ssh.run(
        '/bin/run -d fuchsia-pkg://fuchsia.com/touch-input-test#meta/touch-input-test.cmx');

    expect(await trace, isTrue);
    final traceFile = await helper.performance.downloadTraceFile(testName);

    final metricsSpecSet = MetricsSpecSet(
      testName: testName,
      metricsSpecs: [
        MetricsSpec(name: 'touch_input_latency'),
      ],
    );

    expect(
        await helper.performance.processTrace(metricsSpecSet, traceFile,
            converterPath: _catapultConverterPath,
            registry: touchInputLatencyMetricsRegistry),
        isNotNull);

    // Clean up by killing the processes.  The reason for this is that we want
    // to prevent these processes from interfering with later performance
    // tests.
    await _killProcesses(helper);

    await runApp;
  });
}

void main() {
  enableLoggingOutput();

  _addTest('fuchsia.input_latency.one-flutter');
}
