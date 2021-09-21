// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:core';

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const Duration pollPeriod = Duration(seconds: 1);
const Duration maxHostDiff = Duration(minutes: 1);
const Duration maxUserspaceSystemDiff = Duration(seconds: 2);

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Time sl4fTime;

  setUpAll(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    sl4fTime = sl4f.Time(sl4fDriver);
  });

  tearDownAll(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group('time sync tests', () {
    test('time is synchronized', () async {
      await waitUntilTimeSynchronized(sl4fTime);
      final hostTime = DateTime.now().toUtc();

      // The UTC clock on the DUT (both as measured through the runtime and as
      // measured directly) should roughly align with the host time.
      final systemTime = await sl4fTime.systemTime();
      expect(systemTime.isAfter(hostTime.subtract(maxHostDiff)), isTrue);
      expect(systemTime.isBefore(hostTime.add(maxHostDiff)), isTrue);

      final userspaceTime = await sl4fTime.userspaceTime();
      expect(userspaceTime.isAfter(hostTime.subtract(maxHostDiff)), isTrue);
      expect(userspaceTime.isBefore(hostTime.add(maxHostDiff)), isTrue);
    });

    test('utc clocks agree', () async {
      await waitUntilTimeSynchronized(sl4fTime);

      // Verify that both ways to measure the UTC clock on the DUT roughly
      // agree by checking that their offsets from the host time agree.
      final systemTime = await sl4fTime.systemTime();
      final systemTimeOffset = DateTime.now().toUtc().difference(systemTime);

      final userspaceTime = await sl4fTime.userspaceTime();
      final userspaceTimeOffset =
          DateTime.now().toUtc().difference(userspaceTime);

      expect((systemTimeOffset - userspaceTimeOffset).abs(),
          lessThan(maxUserspaceSystemDiff));
    });
  },

      // While time synchronization is generally expected to complete within a
      // minute, on emulators the timeout may be exceeded if the device is busy
      // even if time synchronization has succeeded.
      timeout: Timeout(Duration(minutes: 2)));
}

Future<void> waitUntilTimeSynchronized(sl4f.Time sl4fTime) async {
  while (!await sl4fTime.isSystemTimeSynchronized()) {
    await Future.delayed(pollPeriod);
  }
}
