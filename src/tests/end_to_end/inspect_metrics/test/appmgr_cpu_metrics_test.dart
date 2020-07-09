// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'util.dart';

const String archivistPath =
    'core/appmgr:root/cpu_stats/measurements/root/app/sys/archivist.cmx';

const String appmgrPath =
    'core/appmgr:root/cpu_stats/measurements/root/appmgr.cm';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Inspect inspect;

  group('appmgr metrics', () {
    setUp(() async {
      sl4fDriver = sl4f.Sl4f.fromEnvironment();
      await sl4fDriver.startServer();
      inspect = sl4f.Inspect(sl4fDriver);

      // Wait until some data shows up for archivist.
      // In E2E tests sometimes the system just started up, and appmgr did not have a chance to expose data on Archivist yet.
      // Appmgr will expose the needed information once it processes the start event for Archvist.
      var values = [];
      while (values.isEmpty) {
        values = await getInspectValues(
            inspect, '$archivistPath/*/@samples/*:cpu_time');
      }
    });

    tearDown(() async {
      await sl4fDriver.stopServer();
      sl4fDriver.close();
    });

    test('appmgr exposes cpu metrics for archivist', () async {
      expect(
          await getInspectValues(
              inspect, '$archivistPath/*/@samples/*:cpu_time'),
          multiValue(greaterThan(0), length: greaterThan(0)));
      expect(
          await getInspectValues(
              inspect, '$archivistPath/*/@samples/*:queue_time'),
          multiValue(greaterThan(0), length: greaterThan(0)));
      expect(
          await getInspectValues(
              inspect, '$archivistPath/*/@samples/*:timestamp'),
          multiValue(greaterThan(0), length: greaterThan(0)));
    });

    test('appmgr exposes cpu metrics for itself', () async {
      expect(
          await getInspectValues(inspect, '$appmgrPath/*/@samples/*:cpu_time'),
          multiValue(greaterThan(0), length: greaterThan(0)));
      expect(
          await getInspectValues(
              inspect, '$appmgrPath/*/@samples/*:queue_time'),
          multiValue(greaterThan(0), length: greaterThan(0)));
      expect(
          await getInspectValues(inspect, '$appmgrPath/*/@samples/*:timestamp'),
          multiValue(greaterThan(0), length: greaterThan(0)));
    });

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
      var size = await getInspectValues(
          inspect, 'core/appmgr:root/inspect_stats:current_size');
      var maxSize = await getInspectValues(
          inspect, 'core/appmgr:root/inspect_stats:maximum_size');

      expect(size, singleValue(greaterThan(0)));
      expect(maxSize, singleValue(greaterThan(0)));
      expect(maxSize, singleValue(greaterThan(size[0])));
    });

    test('appmgr is not out of inspect space for measurements', () async {
      var size = await getInspectValues(inspect,
          'core/appmgr:root/cpu_stats/measurements/@inspect:current_size');
      var maxSize = await getInspectValues(inspect,
          'core/appmgr:root/cpu_stats/measurements/@inspect:maximum_size');

      expect(size, singleValue(greaterThan(0)));
      expect(maxSize, singleValue(greaterThan(0)));
      expect(maxSize, singleValue(greaterThan(size[0])));
    });

    test('appmgr exposes overall recent cpu usage', () async {
      var allParams = await getInspectValues(
          inspect, 'core/appmgr:root/cpu_stats/recent_usage:*');
      expect(allParams, multiValue(isNonNegative, length: equals(6)));
    });
  }, timeout: Timeout(Duration(seconds: 60)));
}
