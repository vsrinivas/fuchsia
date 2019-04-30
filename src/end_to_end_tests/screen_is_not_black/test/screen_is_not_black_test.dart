// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:image/image.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

int _ignoreAlpha(int pixel) => pixel & 0x00ffffff;

/// Returns true if [image] is all black.
bool _isAllBlack(Image image) =>
    image.data.every((pixel) => _ignoreAlpha(pixel) == 0);

/// How many times to check the screen.
const _tries = 10;

/// How long to wait between screen checks.
const _delay = Duration(seconds: 10);

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Scenic scenicDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    scenicDriver = sl4f.Scenic(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('the startup screen is not black', () async {
    for (var attempt = 0; attempt < _tries; attempt++) {
      final screen = await scenicDriver.takeScreenshot(dumpName: 'screen');
      if (!_isAllBlack(screen)) {
        print('Saw a screen that is not black.');
        return;
      }
      await Future.delayed(_delay);
    }
    fail('Screen was all black.');
  },
      // This is a large test that waits for the DUT to come up and to start
      // rendering something.
      timeout: Timeout.none);
}
