// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'util.dart';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Inspect inspect;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    inspect = sl4f.Inspect(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  withLongTimeout(() {
    for (final selector in [
      'cobalt_system_metrics.cmx:root/platform_metrics/temperature:readings',
      'cobalt_system_metrics.cmx:root/platform_metrics/cpu:max',
      'cobalt_system_metrics.cmx:root/platform_metrics/cpu:mean',
    ]) {
      test('legacy metrics includes $selector', () async {
        expect(
            await getInspectValues(inspect, selector,
                pipeline: sl4f.InspectPipeline.legacyMetrics),
            singleValue(isNotEmpty));
      });
    }

    test('legacy metrics includes historical_max_cpu_temperature_c', () async {
      // Verify the `historical_max_cpu_temperature_c` node is present
      expect(
          await getInspectValues(inspect,
              'bootstrap/power_manager:root/platform_metrics/historical_max_cpu_temperature_c:*',
              pipeline: sl4f.InspectPipeline.legacyMetrics),
          multiValue(isNotNull, length: greaterThan(0)));
    });
  });
}
