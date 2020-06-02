// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'util.dart';

const String archivistPath =
    'core/appmgr:root/cpu_stats/measurements/root/app/sys/archivist.cmx';

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

  test('appmgr exposes cpu metrics for archivist', () async {
    final inspect = sl4f.Inspect(sl4fDriver);

    expect(
        await getInspectValues(inspect, '$archivistPath/*/@samples/*:cpu_time'),
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

  test('appmgr exposes stats on measurement time', () async {
    final inspect = sl4f.Inspect(sl4fDriver);

    expect(
        await getInspectValues(
            inspect, 'core/appmgr:root/cpu_stats:process_time_ns'),
        singleValue(isNotEmpty));
  });

  test('appmgr exposes the number of tracked tasks', () async {
    final inspect = sl4f.Inspect(sl4fDriver);

    expect(
        await getInspectValues(
            inspect, 'core/appmgr:root/cpu_stats:task_count'),
        singleValue(greaterThan(0)));
  });
}
