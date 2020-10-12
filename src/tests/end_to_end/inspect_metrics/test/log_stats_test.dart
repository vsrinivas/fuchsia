// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'util.dart';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Inspect inspect;

  tearDownAll(printErrorHelp);

  group('log stats metrics', () {
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
      test('log-stats exposes kernel log metrics', () async {
        expect(
            (await getInspectValues(
                inspect, 'core/log-stats:root:kernel_logs'))[0],
            isPositive);
      });

      test('log-stats exposes per-component metrics', () async {
        var byComponent = (await getInspectTree(
                inspect, 'core/log-stats:root/by_component/*:*'))[0]
            ['by_component'];
        expect(
            byComponent.keys,
            containsAll([
              'fuchsia-pkg://fuchsia.com/log-stats#meta/log-stats.cm',
              'fuchsia-boot://kernel',
            ]));
      });
    });
  });
}
