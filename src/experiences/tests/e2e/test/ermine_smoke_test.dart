// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - Connect to ermine using Flutter Driver.
///  - Ensure its screenshot is not all black.
void main() {
  Sl4f sl4f;
  FlutterDriverConnector connector;
  FlutterDriver driver;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    // Check if ermine is up and registered itself with [Inspect].
    final inspect = Inspect(sl4f.ssh);
    final ermine = await inspect.inspectComponentRoot('ermine');
    if (ermine == null) {
      fail('could not find ermine\'s inspect node');
    }

    connector = FlutterDriverConnector(sl4f);
    await connector.initialize();

    // Check if ermine is running.
    final isolate = await connector.isolate('ermine');
    if (isolate == null) {
      fail('could not find ermine.');
    }

    // Now connect to ermine.
    driver = await connector.driverForIsolate('ermine');
    if (driver == null) {
      fail('unable to connect to ermine.');
    }
  });

  tearDownAll(() async {
    await driver?.close();
    await connector.tearDown();
    await sl4f.stopServer();
    sl4f.close();
  });

  test('Screen should not be black', () async {
    await driver.waitUntilNoTransientCallbacks();

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
