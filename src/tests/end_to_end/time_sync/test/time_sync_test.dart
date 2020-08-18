// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:core';
import 'dart:io';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Time sl4fTime;

  const Duration pollPeriod = Duration(seconds: 1);

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
    while (!await sl4fTime.isSystemTimeSynchronized()) {
      sleep(pollPeriod);
    }
    final systemTime = await sl4fTime.systemTime();
    // Do a fairly permissive check as it takes time to check the time
    // and the host may be set slightly different.
    final hostTime = DateTime.now().toUtc();
    expect(systemTime.isAfter(hostTime.subtract(Duration(minutes: 1))), isTrue);
    expect(systemTime.isBefore(hostTime.add(Duration(minutes: 1))), isTrue);
  });
}
