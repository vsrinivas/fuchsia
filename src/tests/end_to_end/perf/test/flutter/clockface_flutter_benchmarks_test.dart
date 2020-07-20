// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const _catapultConverterPath = 'runtime_deps/catapult_converter';
const _trace2jsonPath = 'runtime_deps/trace2json';
const _testName = 'fuchsia.flutter.clockface_benchmarks';
const _appName = 'clockface-flutter';

/// Duration for which to run the benchmark.
///
/// Note that this includes the startup time of the app.
const _duration = Duration(seconds: 30); // seconds

void main() {
  test('trace clockface for $_duration seconds', () async {
    final helper = await FlutterTestHelper.make();

    final traceSession = await helper.perf.initializeTracing(categories: [
      'flutter',
      'gfx',
      'kernel:meta',
    ]);
    await traceSession.start();

    await helper.addTile(_appName);

    await Future.delayed(_duration);

    await traceSession.stop();
    final fxtTraceFile = await traceSession.terminateAndDownload(_testName);
    final jsonTraceFile =
        await helper.perf.convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    final flutterExtraArgs = {'flutterAppName': _appName};
    final List<MetricsSpec> metricsSpecs = [
      MetricsSpec(name: 'drm_fps', extraArgs: flutterExtraArgs),
      MetricsSpec(name: 'flutter_frame_stats', extraArgs: flutterExtraArgs),
      MetricsSpec(name: 'scenic_frame_stats'),
      MetricsSpec(name: 'flutter_startup_time', extraArgs: flutterExtraArgs),
    ];

    await helper.perf.processTrace(
      MetricsSpecSet(metricsSpecs: metricsSpecs, testName: _testName),
      jsonTraceFile,
      converterPath: _catapultConverterPath,
      registry: FlutterTestHelper.metricsRegistry,
    );
  }, timeout: Timeout(Duration(minutes: 2)));
}
