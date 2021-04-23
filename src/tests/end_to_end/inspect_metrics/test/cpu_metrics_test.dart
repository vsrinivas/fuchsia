// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'util.dart';

const String appmgrPath =
    'core/appmgr:root/cpu_stats/measurements/root/appmgr.cm';
const String componentManagerPath =
    '<component_manager>:root/cpu_stats/measurements/components/<component_manager>';
const String componentManagerAppmgrPath =
    '<component_manager>:root/cpu_stats/measurements/components/core\\/appmgr';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Inspect inspect;

  group('cpu metrics', () {
    setUp(() async {
      sl4fDriver = sl4f.Sl4f.fromEnvironment();
      await sl4fDriver.startServer();
      inspect = sl4f.Inspect(sl4fDriver);
    });

    tearDown(() async {
      await sl4fDriver.stopServer();
      sl4fDriver.close();
    });

    tearDownAll(printErrorHelp);

    withLongTimeout(() {
      test('appmgr exposes stats on measurement time', () async {
        expect(
            await getInspectValues(
                inspect, 'core/appmgr:root/cpu_stats:process_time_ns'),
            singleValue(isNotEmpty));
      });

      test('appmgr exposes the number of tracked tasks', () async {
        expect(
            await getInspectValues(
                inspect, 'core/appmgr:root/cpu_stats:task_count'),
            singleValue(greaterThan(0)));
      });

      test('appmgr is not out of inspect space', () async {
        final size = await getInspectValues(
            inspect, 'core/appmgr:root/inspect_stats:current_size');
        final maxSize = await getInspectValues(
            inspect, 'core/appmgr:root/inspect_stats:maximum_size');

        expect(size, singleValue(greaterThan(0)));
        expect(maxSize, singleValue(greaterThan(0)));
        expect(maxSize, singleValue(greaterThan(size[0])));
      });

      test('appmgr is not out of inspect space for measurements', () async {
        final size = await getInspectValues(inspect,
            'core/appmgr:root/cpu_stats/measurements/@inspect:current_size');
        final maxSize = await getInspectValues(inspect,
            'core/appmgr:root/cpu_stats/measurements/@inspect:maximum_size');

        expect(size, singleValue(greaterThan(0)));
        expect(maxSize, singleValue(greaterThan(0)));
      });

      test('appmgr exposes overall recent cpu usage', () async {
        final allParams = await getInspectValues(
            inspect, 'core/appmgr:root/cpu_stats/recent_usage:*');
        expect(allParams, multiValue(isNonNegative, length: equals(6)));
      });

      test('component manager exposes cpu metrics for components', () async {
        expect(
            await getInspectValues(
                inspect, '$componentManagerAppmgrPath/*/@samples/*:cpu_time'),
            multiValue(greaterThanOrEqualTo(0), length: greaterThan(0)));
        expect(
            await getInspectValues(
                inspect, '$componentManagerAppmgrPath/*/@samples/*:queue_time'),
            multiValue(greaterThanOrEqualTo(0), length: greaterThan(0)));
        expect(
            await getInspectValues(
                inspect, '$componentManagerAppmgrPath/*/@samples/*:timestamp'),
            multiValue(greaterThan(0), length: greaterThan(0)));
      });

      test('component manager exposes cpu metrics for itself', () async {
        expect(
            await getInspectValues(
                inspect, '$componentManagerPath/*/@samples/*:cpu_time'),
            multiValue(greaterThan(0), length: greaterThan(0)));
        expect(
            await getInspectValues(
                inspect, '$componentManagerPath/*/@samples/*:queue_time'),
            multiValue(greaterThan(0), length: greaterThan(0)));
        expect(
            await getInspectValues(
                inspect, '$componentManagerPath/*/@samples/*:timestamp'),
            multiValue(greaterThan(0), length: greaterThan(0)));
      });

      test('component manager exposes stats on measurement time', () async {
        expect(
            await getInspectValues(inspect,
                '<component_manager>:root/cpu_stats:processing_times_ns'),
            singleValue(isNotEmpty));
      });

      test('component manager exposes the number of tracked tasks', () async {
        expect(
            await getInspectValues(inspect,
                '<component_manager>:root/cpu_stats/measurements:task_count'),
            singleValue(greaterThan(0)));
      });

      test('component manager is not out of inspect space', () async {
        final size = await getInspectValues(
            inspect, '<component_manager>:root/inspect_stats:current_size');
        final maxSize = await getInspectValues(
            inspect, '<component_manager>:root/inspect_stats:maximum_size');

        expect(size, singleValue(greaterThan(0)));
        expect(maxSize, singleValue(greaterThan(0)));
        expect(maxSize, singleValue(greaterThan(size[0])));
      });

      test('component manager is not out of inspect space for measurements',
          () async {
        final size = await getInspectValues(inspect,
            '<component_manager>:root/cpu_stats/measurements/inspect_stats:current_size');
        final maxSize = await getInspectValues(inspect,
            '<component_manager>:root/cpu_stats/measurements/inspect_stats:maximum_size');

        expect(size, singleValue(greaterThan(0)));
        expect(maxSize, singleValue(greaterThan(0)));
      });

      test('component manager exposes overall recent cpu usage', () async {
        final allParams = await getInspectValues(
            inspect, '<component_manager>:root/cpu_stats/recent_usage:*');
        // Either 3 or 6. It might contain the previous recent already.
        expect(allParams,
            multiValue(isNonNegative, length: anyOf(equals(6), equals(3))));
      });
    });
  });
}
