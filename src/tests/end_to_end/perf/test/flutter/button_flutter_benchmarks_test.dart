// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:sl4f/sl4f.dart';
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const _catapultConverterPath = 'runtime_deps/catapult_converter';
const _trace2jsonPath = 'runtime_deps/trace2json';
const _testName = 'fuchsia.flutter.button_benchmark';
const _appName = 'button_flutter';

const _numberOfTaps = 100;

// On existing hardware, input latency is typically 50 ms or lower, so an
// interval of at least 50 ms between taps should help ensure no two
// inputs arrive at the "same time". This uses a base latency of 100 ms,
// which is equal to six frames at 60 fps.
// Add 1/6000 s (0.166... ms) so taps happen at different points
// throughout the vsync interval. 100 taps will then happen at 100
// equidistant points spanning the vsync interval.
const double _millisecondsBetweenTaps = 100 + 1 / 6;

void main() {
  print(_millisecondsBetweenTaps);
  test('press button', () async {
    final helper = await FlutterTestHelper.make();
    final input = Input(helper.sl4f);

    final traceSession = await helper.perf.initializeTracing(categories: [
      'input',
      'flutter',
      'gfx',
      'kernel:meta',
      'magma',
    ]);
    await traceSession.start();

    await helper.addTile(_appName);

    // Wait for the app to start.
    // TODO(55832): Use flutter_driver waitUntilNoTransientCallbacks.
    await Future.delayed(Duration(seconds: 15));

    // The button should be in the center of the screen; that is, (500, 500).
    // TODO(55832): Find the button position using flutter_driver.
    const buttonPosition = Point<int>(500, 500);

    const totalDuration = _numberOfTaps * _millisecondsBetweenTaps;

    await input.tap(buttonPosition,
        tapEventCount: _numberOfTaps, duration: totalDuration.toInt());

    await Future.delayed(Duration(seconds: 5));

    await traceSession.stop();

    final fxtTraceFile = await traceSession.terminateAndDownload(_testName);
    final jsonTraceFile =
        await helper.perf.convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    final List<MetricsSpec> metricsSpecs = [
      MetricsSpec(
        name: 'flutter_startup_time',
        extraArgs: {'flutterAppName': _appName},
      ),
      MetricsSpec(name: 'input_latency'),
    ];

    await helper.perf.processTrace(
      MetricsSpecSet(metricsSpecs: metricsSpecs, testName: _testName),
      jsonTraceFile,
      converterPath: _catapultConverterPath,
      registry: FlutterTestHelper.metricsRegistry,
    );
  }, timeout: Timeout(Duration(minutes: 2)));
}
