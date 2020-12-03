// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'ermine_driver.dart';

/// Tests that the DUT running ermine can do the following:
///  - Connect to ermine using Flutter Driver.
///  - Ensure its screenshot is not all black.
void main() {
  Sl4f sl4f;
  ErmineDriver ermine;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();
  });

  tearDownAll(() async {
    // Any of these may end up being null if the test fails in setup.
    await ermine.tearDown();
    await sl4f?.stopServer();
    sl4f?.close();
  });

  test('Screen should not be black', () async {
    await ermine.driver.waitUntilNoTransientCallbacks();

    // Now take a screen shot and make sure it is not all black.
    final scenic = Scenic(sl4f);
    final image = await scenic.takeScreenshot();
    bool isAllBlack = image.data.every((pixel) => pixel & 0x00ffffff == 0);
    expect(isAllBlack, false);
  });
}

Future<Point<int>> centerFromFinder(
    FlutterDriver driver, SerializableFinder finder) async {
  // Get the bottom right of the main screen.
  final mainScreenFinder = find.byValueKey('main');
  final bottomRight = await driver.getBottomRight(mainScreenFinder);

  // The `input` utility expects screen coordinates to be scaled 0 - 1000.
  final center = await driver.getCenter(finder);
  int x = (center.dx / bottomRight.dx * 1000).toInt();
  int y = (center.dy / bottomRight.dy * 1000).toInt();

  return Point<int>(x, y);
}
