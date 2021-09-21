// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const _timeout = Timeout(Duration(minutes: 1));

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.DeviceLog logging;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    logging = sl4f.DeviceLog(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.DeviceLog, () {
    test('talks to SL4F server without error', () async {
      // If anything throws an exception then we've failed.
      await logging.info('info level test');
      await logging.warn('warn level test');
      await logging.error('error level test');
    });
  }, timeout: _timeout);
}
