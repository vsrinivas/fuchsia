// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'util.dart';

void main() {
  sl4f.Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  for (final selector in [
    'cobalt_system_metrics.cmx:root/platform_metrics/temperature:readings',
    'cobalt_system_metrics.cmx:root/platform_metrics/cpu:max',
    'cobalt_system_metrics.cmx:root/platform_metrics/cpu:mean',
  ]) {
    test('legacy metrics includes $selector', () async {
      final inspect = sl4f.Inspect(sl4fDriver);
      expect(
          await getInspectValues(inspect, selector,
              pipeline: sl4f.InspectPipeline.legacyMetrics),
          singleValue(isNotEmpty));
    });
  }
}
