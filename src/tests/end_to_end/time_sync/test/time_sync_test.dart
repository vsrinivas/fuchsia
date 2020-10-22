// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:core';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const Duration pollPeriod = Duration(seconds: 1);
const Duration maxHostDiff = Duration(minutes: 1);
const Duration maxUserspaceKernelDiff = Duration(seconds: 2);

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Time sl4fTime;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    sl4fTime = sl4f.Time(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('time is synchronized', () async {
    await waitUntilTimeSynchronized(sl4fTime);
    final hostTime = DateTime.now().toUtc();

    // All UTC clocks on the DUT should roughly align with the host time.
    final systemTime = await sl4fTime.systemTime();
    expect(systemTime.isAfter(hostTime.subtract(maxHostDiff)), isTrue);
    expect(systemTime.isBefore(hostTime.add(maxHostDiff)), isTrue);

    final userspaceTime = await sl4fTime.userspaceTime();
    expect(userspaceTime.isAfter(hostTime.subtract(maxHostDiff)), isTrue);
    expect(userspaceTime.isBefore(hostTime.add(maxHostDiff)), isTrue);

    final kernelTime = await sl4fTime.kernelTime();
    expect(kernelTime.isAfter(hostTime.subtract(maxHostDiff)), isTrue);
    expect(kernelTime.isBefore(hostTime.add(maxHostDiff)), isTrue);
  });

  test('utc clocks agree', () async {
    await waitUntilTimeSynchronized(sl4fTime);

    // Verify that all UTC clocks on the DUT roughly agree by checking that
    // their offsets from the host time agree.
    final systemTime = await sl4fTime.systemTime();
    final systemTimeOffset = DateTime.now().toUtc().difference(systemTime);

    final userspaceTime = await sl4fTime.userspaceTime();
    final userspaceTimeOffset =
        DateTime.now().toUtc().difference(userspaceTime);

    final kernelTime = await sl4fTime.kernelTime();
    final kernelTimeOffset = DateTime.now().toUtc().difference(kernelTime);

    expect((systemTimeOffset - userspaceTimeOffset).abs(),
        lessThan(maxUserspaceKernelDiff));
    expect((systemTimeOffset - kernelTimeOffset).abs(),
        lessThan(maxUserspaceKernelDiff));
    expect((userspaceTimeOffset - kernelTimeOffset).abs(),
        lessThan(maxUserspaceKernelDiff));
  });
}

Future<void> waitUntilTimeSynchronized(sl4f.Time sl4fTime) async {
  while (!await sl4fTime.isSystemTimeSynchronized()) {
    await Future.delayed(pollPeriod);
  }
}
