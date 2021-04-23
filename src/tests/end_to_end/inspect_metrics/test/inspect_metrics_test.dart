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

  tearDownAll(printErrorHelp);

  withLongTimeout(() {
    test('inspect metrics tests inspection nodes written by metrics', () async {
      // The test will read each of the node for disk usages
      // fx set ... --release --with=//src/tests/end_to_end/inspect_metrics:test --with src/testing/sl4f:bin
      // fx build
      // fx run-e2e-tests inspect_metrics_test # which sets FUCHSIA_IPV4_ADDR automatically

      final inspect = sl4f.Inspect(sl4fDriver);

      // The following paths are used by reader code in b/152262213. Please notify if the paths are moved.
      // There should be plan on how to move the path for the reader side to work before and after the move.
      // Refer to design doc in go/fuchsia-metrics-to-inspect-design.
      expect(
          await getInspectValues(
              inspect, 'bootstrap/fshost:root/data_stats/data/cache:size'),
          singleValue(isNonZero));
      expect(
          await getInspectValues(
              inspect, 'bootstrap/fshost:root/data_stats/stats:used_bytes'),
          singleValue(isNonZero));
    });
  });
}
