// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/sl4f.dart';
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'helpers.dart';

// TODO(23043): Pass this into [Performance.processTrace] once the full sl4f
// migration is complete.
// ignore: unused_element
const String _catapultConverterPath = 'runtime_deps/catapult_converter';

Future<void> _killProcesses(PerfTestHelper helper) async {
  await helper.sl4fDriver.ssh.run('killall "root_presenter*"');
  await helper.sl4fDriver.ssh.run('killall "scenic*"');
  await helper.sl4fDriver.ssh.run('killall "basemgr*"');
  await helper.sl4fDriver.ssh.run('killall "flutter*"');
  await helper.sl4fDriver.ssh.run('killall "present_view*"');
  await helper.sl4fDriver.ssh.run('killall "simplest_app*"');
}

void _addDeprecatedTests() {
  void addTest(String label, String componentUrl) {
    test(label, () async {
      final helper = await PerfTestHelper.make();
      const resultsFile = '/tmp/perf_results.json';
      final result = await helper.sl4fDriver.ssh
          .run('/bin/run $componentUrl --out_file $resultsFile'
              ' --benchmark_label $label');
      expect(result.exitCode, equals(0));
      await helper.processResults(resultsFile);
    }, timeout: Timeout.none);
  }

  const baseUrl = 'fuchsia-pkg://fuchsia.com/garnet_input_latency_benchmarks';

  addTest('fuchsia.input_latency.simplest_app',
      '$baseUrl#meta/run_simplest_app_benchmark.cmx');
  addTest('fuchsia.input_latency.yuv_to_image_pipe',
      '$baseUrl#meta/run_yuv_to_image_pipe_benchmark.cmx');
}

void _addReplacementTest(String testName, String runAppCommand) {
  test(testName, () async {
    final helper = await PerfTestHelper.make();

    await _killProcesses(helper);

    final runApp = helper.sl4fDriver.ssh.run(runAppCommand);

    // Wait for the application to start.
    await Future.delayed(Duration(seconds: 3));

    // Start tracing.
    final trace = helper.performance.trace(
        duration: Duration(seconds: 5),
        traceName: testName,
        categories: 'input,gfx,magma',
        bufferSize: 36);

    await Future.delayed(Duration(seconds: 1));

    // Each tap will be 33.5ms apart, drifting 0.166ms against regular 60 fps
    // vsync interval. 100 taps span the entire vsync interval 1 time at 100
    // equidistant points.
    await helper.sl4fDriver.ssh
        .run('/bin/input tap 500 500 --tap_event_count=100 --duration=3350');

    expect(await trace, isTrue);
    final traceFile = await helper.performance.downloadTraceFile(testName);

    final metricsSpecSet = MetricsSpecSet(
      testName: testName,
      metricsSpecs: [
        MetricsSpec(name: 'input_latency'),
      ],
    );

    // TODO(23043): Pass [_converterPath] in here once the full sl4f migration
    // is complete.
    expect(
        await helper.performance
            .processTrace(metricsSpecSet, traceFile, converterPath: null),
        isNotNull);

    // Clean up by killing the processes.  The reason for this is that we want
    // to prevent these processes from interfering with later performance
    // tests.
    await _killProcesses(helper);

    await runApp;
  });
}

// TODO(23043): These don't upload results to Catapult yet.
void _addReplacementTests() {
  _addReplacementTest('fuchsia.input_latency.simplest_app.DISABLED',
      '/bin/run -d fuchsia-pkg://fuchsia.com/simplest_app#meta/simplest_app.cmx');
  _addReplacementTest(
      'fuchsia.input_latency.yuv_to_image_pipe.DISABLED',
      '/bin/present_view '
          'fuchsia-pkg://fuchsia.com/yuv_to_image_pipe#meta/yuv_to_image_pipe.cmx '
          '--NV12 --input_driven');
}

void main() {
  enableLoggingOutput();

  _addDeprecatedTests();
  _addReplacementTests();
}
