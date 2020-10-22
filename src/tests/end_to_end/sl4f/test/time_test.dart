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
    // In context of these tests, we don't know if the device should actually be synchronized
    // or not, so we just verify that we can get some time.
    final earlyTime = DateTime.utc(2000, 1, 1);

    test('can retrieve system time', () async {
      final systemTime = await time.systemTime();
      expect(systemTime.isAfter(earlyTime), isTrue);
    });

    test('can retrieve userspace time', () async {
      final userspaceTime = await time.userspaceTime();
      expect(userspaceTime.isAfter(earlyTime), isTrue);
    });

    test('can retrieve kernel time', () async {
      final kernelTime = await time.kernelTime();
      expect(kernelTime.isAfter(earlyTime), isTrue);
    });

    test('can check device synchronized', () async {
      expect(await time.isSystemTimeSynchronized(), isNotNull);
    });
  });
}
