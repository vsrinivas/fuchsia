// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void main() {
  sl4f.Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('tests isRunning correctly returns true', () async {
    await sl4fDriver.startServer();
    final result = await sl4fDriver.isRunning();
    expect(result, isTrue);
  }, timeout: Timeout.none);

  test('tests isRunning correctly returns false', () async {
    // Do not start server
    final result = await sl4fDriver.isRunning();
    expect(result, isFalse);
  }, timeout: Timeout.none);
}
