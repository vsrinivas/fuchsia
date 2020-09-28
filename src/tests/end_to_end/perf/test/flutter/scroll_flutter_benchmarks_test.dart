// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const _catapultConverterPath = 'runtime_deps/catapult_converter';
const _trace2jsonPath = 'runtime_deps/trace2json';
const _testName = 'fuchsia.flutter.scroll_benchmarks';
const _appName = 'scroll-flutter';

const _scrollDuration = Duration(milliseconds: 200);

void main() {
  test('scroll_flutter', () async {
    final helper = await FlutterTestHelper.make();

    final traceSession = await helper.perf.initializeTracing(categories: [
      'input',
      'flutter',
      'gfx',
      // TODO(fxbug.dev/56996): Adding 'kernel:meta' here causes the scroll-flutter.cmx.ui
      // thread to be unnamed in the trace, which in turn causes the drm_fps
      // metric to fail.
    ]);
    await traceSession.start();

    await helper.addTile(_appName, args: ['--flutter-driver']);
    await helper.connectFlutterDriver(_appName);
    await helper.driver.waitUntilNoTransientCallbacks();

    final scrollableWidget = ByType('ListView');

    // The vertical offset by which to scroll.
    final dy = (await helper.driver.getBottomLeft(scrollableWidget)).dy;

    Future<void> scrollDown() async =>
        await helper.driver.scroll(scrollableWidget, 0, -dy, _scrollDuration);
    Future<void> scrollUp() async =>
        await helper.driver.scroll(scrollableWidget, 0, dy, _scrollDuration);

    await scrollDown();
    await scrollUp();
    await scrollUp();
    await scrollDown();

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
  }, timeout: Timeout(Duration(seconds: 60)));
}
