// Copyright 2020 The Fuchsia Authors. All rights reserved.
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
    await sl4fDriver.startServer();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('tests harvester --once exits cleanly', () async {
    expect(
        await sl4f.Component(sl4fDriver)
            .launch('system_monitor_harvester', ['--once', '--local']),
        'Success');
  }, timeout: Timeout(Duration(seconds: 60)));
}
