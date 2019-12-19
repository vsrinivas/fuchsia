// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - Connect to base shell (userpicker_base_shell) using Flutter Driver.
///  - Tap login button using scenic input utility.
///  - Connect to user shell (ermine) using Flutter Driver.
///  - Ensure its screenshot is not all black.
///  - Log out of user shell (ermine).
void main() {
  Sl4f sl4f;
  FlutterDriverConnector connector;
  FlutterDriver driver;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    connector = FlutterDriverConnector(sl4f);
    await connector.initialize();

    // Check if base shell is running.
    final isolate = await connector.isolate('userpicker_base_shell');
    if (isolate == null) {
      fail('could not find userpicker_base_shell.');
    }

    // Now connect to base shell.
    driver = await connector.driverForIsolate('userpicker_base_shell');
    if (driver == null) {
      fail('unable to connect to userpicker_base_shell.');
    }
  });

  tearDownAll(() async {
    await driver.close();
    await connector.tearDown();
    await sl4f.stopServer();
    sl4f.close();
  });

  test('Guest login to ermine', () async {
    // Get the center of the plus key.
    final plusButtonFinder = find.byValueKey('plus');
    var center = await centerFromFinder(driver, plusButtonFinder);

    final input = Input(sl4f);
    bool didTap = await input.tap(Point(center.x, center.y));
    expect(didTap, true);

    // The 'Guest' button should be visible, tap it.
    final guestButtonFinder = find.byValueKey('Guest');
    center = await centerFromFinder(driver, guestButtonFinder);

    didTap = await input.tap(Point(center.x, center.y));
    expect(didTap, true);

    // This should launch ermine.
    final ermineDriver = await connector.driverForIsolate('ermine');
    if (ermineDriver == null) {
      fail('Unable to launch ermine');
    }
    await ermineDriver.waitUntilNoTransientCallbacks();

    // Now take a screen shot and make sure it is not all black.
    final scenic = Scenic(sl4f);
    final image = await scenic.takeScreenshot();
    bool isAllBlack = image.data.every((pixel) => pixel & 0x00ffffff == 0);
    expect(isAllBlack, false);

    // Now log out of ermine.
    final logoutFinder = find.byValueKey('logout');
    await ermineDriver.tap(logoutFinder);
    await ermineDriver.close();
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
