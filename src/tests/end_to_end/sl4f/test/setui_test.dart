// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Timeout(Duration(minutes: 1));

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.SetUi setUi;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    setUi = sl4f.SetUi(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.SetUi, () {
    test('talks to SL4F mutateLoginOverride without error', () async {
      // If anything throws an exception then we've failed.
      await setUi.mutateLoginOverride(sl4f.LoginOverride.none);
    });
    test('talks to SL4F getDevNetworkOption without error', () async {
      // If anything throws an exception then we've failed.
      await setUi.getDevNetworkOption();
    });
  }, timeout: _timeout);
}
