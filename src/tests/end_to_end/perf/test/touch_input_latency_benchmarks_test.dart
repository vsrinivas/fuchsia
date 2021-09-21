// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const String _catapultConverterPath = 'runtime_deps/catapult_converter';
const String _trace2jsonPath = 'runtime_deps/trace2json';

const _touchInputLatencyMetricsRegistry = {
  'touch_input_latency': touchInputLatencyMetricsProcessor,
};

// TODO(fxbug.dev/76494): Reenable with independent test.
// ignore: unused_element
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

  // TODO(fxbug.dev/76494): Reenable with independent test.
  // _addTest('fuchsia.input_latency.one-flutter');
}
