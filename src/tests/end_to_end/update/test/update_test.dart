// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Timeout(Duration(minutes: 1));

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Update update;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    update = sl4f.Update(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.Update, () {
    test('update sanity test', () async {
      // If anything throws an exception then we've failed.
      await update.getState();
      await update.getCurrentChannel();
      await update.getTargetChannel();
      await update.getChannelList();
    });
  }, timeout: _timeout);
}
