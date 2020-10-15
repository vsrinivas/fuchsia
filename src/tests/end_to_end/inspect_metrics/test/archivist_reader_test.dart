// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests that the archivist read data from both v1 and v2 components.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'util.dart';

const String fshostPath = 'bootstrap/fshost:root/data_stats';
const String memoryMonitorPath = 'memory_monitor.cmx:root';

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

  tearDownAll(printErrorHelp);

  withLongTimeout(() {
    test('archivist can read both v1 and v2 component data', () async {
      expect(await getInspectValues(inspect, '$fshostPath/data/cache:size'),
          singleValue(greaterThan(0)));
      expect(await getInspectValues(inspect, '$fshostPath/stats:used_bytes'),
          singleValue(greaterThan(0)));
      // Note: when memory monitor becomes a v2 component, pick a v1 component
      // that starts early on.
      expect(
          await getInspectValues(inspect, '$memoryMonitorPath:current_digest'),
          allOf(
              isNotNull,
              contains(allOf([
                contains('Archivist'),
                contains('Audio'),
                contains('Kernel'),
                contains('Free'),
                contains('Minfs'),
                contains('Pkgfs'),
                contains('Graphics'),
                contains('Flutter')
              ]))));
    });

    test('read from the feedback accessor', () async {
      expect(
          await getInspectValues(
            inspect,
            'bootstrap/archivist:root/fuchsia.inspect.Health:status',
            pipeline: sl4f.InspectPipeline.feedback,
          ),
          equals(['OK']));
    });
  });
}
