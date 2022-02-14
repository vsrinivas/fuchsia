// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language
// version.
// @dart=2.9

import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const _benchmarkDuration = Duration(seconds: 10);

const String _catapultConverterPath = 'runtime_deps/catapult_converter';
const String _trace2jsonPath = 'runtime_deps/trace2json';

Future<void> _killProcesses(PerfTestHelper helper) async {
  print('Killing processes for flatland_benchmarks_test');
  await helper.sl4fDriver.ssh.run('killall "basemgr*"');
  await helper.sl4fDriver.ssh.run('killall "a11y-manager*"');
  await helper.sl4fDriver.ssh.run('killall "present_view*"');
  await helper.sl4fDriver.ssh.run('killall "root_presenter*"');
  await helper.sl4fDriver.ssh.run('killall "scenic*"');
  await helper.sl4fDriver.ssh.run('killall "tiles*"');
  await helper.sl4fDriver.ssh.run('killall "flutter*"');
  await helper.sl4fDriver.ssh.run('killall "flatland-view-provider*"');
  print('Finished killing processes for flatland_benchmarks_test');
}

void _addTest(String testName, String appUrl) {
  test(testName, () async {
    final helper = await PerfTestHelper.make();

    await helper.sl4fDriver.ssh.run(
        'log flatland_benchmarks_test "Killing processes for flatland_benchmarks_test, before test"');
    await _killProcesses(helper);
    await helper.sl4fDriver.ssh.run(
        'log flatland_benchmarks_test "Finished killing processes for flatland_benchmarks_test, before test"');

    final tiles = Tiles(helper.sl4fDriver);
    await tiles.startFlatland();

    final tileKey = await tiles.addFromUrl(appUrl);

    // Wait for the application to start.
    await Future.delayed(Duration(seconds: 3));

    // Start tracing.
    final traceSession = await helper.performance.initializeTracing(
        categories: [
          'input',
          'gfx',
          'magma',
          'system_metrics',
          'system_metrics_logger'
        ],
        bufferSize: 36);

    await traceSession.start();

    await Future.delayed(_benchmarkDuration);

    await traceSession.stop();

    await tiles.remove(tileKey);
    await tiles.stop();

    final fxtTraceFile = await traceSession.terminateAndDownload(testName);
    final jsonTraceFile = await helper.performance
        .convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    final metricsSpecSet = MetricsSpecSet(
      testName: testName,
      metricsSpecs: [
        MetricsSpec(name: 'flatland_latency'),
        MetricsSpec(name: 'cpu'),
        MetricsSpec(name: 'gpu'),
      ],
    );

    expect(
        await helper.performance.processTrace(metricsSpecSet, jsonTraceFile,
            converterPath: _catapultConverterPath),
        isNotNull);

    // Clean up by killing the processes.  The reason for this is that we want
    // to prevent these processes from interfering with later performance
    // tests.
    await helper.sl4fDriver.ssh.run(
        'log flatland_benchmarks_test "Killing processes for flatland_benchmarks_test, after test"');
    await _killProcesses(helper);
    await helper.sl4fDriver.ssh.run(
        'log flatland_benchmarks_test "Finished killing processes for flatland_benchmarks_test, after test"');
  });
}

void main() {
  enableLoggingOutput();

  _addTest('fuchsia.flatland_latency.view-provider-example',
      'fuchsia-pkg://fuchsia.com/flatland-examples#meta/flatland-view-provider-example.cmx');
}
