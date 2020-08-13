// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Time time;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    time = sl4f.Time(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group('time', () {
    test('can retrieve time', () async {
      // In context of this test, we don't know if the device should actually be synchronized
      // or not, so just check that we can get some time.
      final earlyTime = DateTime.utc(2000, 1, 1);
      final systemTime = await time.systemTime();
      expect(systemTime.isAfter(earlyTime), isTrue);
      expect(await time.isSystemTimeSynchronized(), isNotNull);
    });
  });
}
