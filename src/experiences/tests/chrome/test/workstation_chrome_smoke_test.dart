// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: import_of_legacy_library_into_null_safe
import 'dart:math';

import 'package:ermine_driver/ermine_driver.dart';
import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

const chromiumUrl = 'fuchsia-pkg://chromium.org/chrome#meta/chrome.cm';

void main() {
  late Sl4f sl4f;
  late ErmineDriver ermine;

  setupLogger(name: 'workstation_chrome_smoke_test');

  Logger.root.onRecord.listen((r) => print('${r.level}: ${r.message}'));

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();
  });

  tearDownAll(() async {
    await ermine.tearDown();
    print('Tore down Ermine flutter driver');
    await sl4f.stopServer();
    print('Stopped sl4f server');
    sl4f.close();
    print('Closed sl4f');
  });

  test('Should be able to launch Chrome browser.', () async {
    // Launches Chromium app
    // TODO(fxb/94441): Launch Chromium using [ErmineDriver.launch] once the blocker is fixed.
    final chromiumEntry = find.text('Chromium');
    await ermine.driver.waitFor(chromiumEntry);
    print('Found Chromium app entry');
    await ermine.driver.tap(chromiumEntry);
    print('Tapped Chromium app entry');
    await ermine.driver.waitUntilNoTransientCallbacks();
    print('Launched Chromium');

    final snapshot = await ermine.waitForView(chromiumUrl, testForFocus: true);
    expect(snapshot.url, chromiumUrl);
    print('A Chromium view is presented');

    // Takes a screenshot and checks the color
    const white = 0xffffffff; // (0xAABBGGRR)
    Map<int, int> histogram;

    await Future.delayed(Duration(seconds: 3));
    final isWhite = await ermine.waitFor(() async {
      print('Take a screenshot...');
      final screenshot = await ermine.screenshot(Rectangle(500, 500, 100, 100));
      histogram = ermine.histogram(screenshot);
      print('Color key: ${histogram.keys.first}');
      print('Color value: ${histogram.values.first}');
      if (histogram.keys.length == 1 && histogram[white] == 10000) {
        return true;
      }
      return false;
    }, timeout: Duration(minutes: 2));

    expect(isWhite, isTrue);
    print('Verified the expected background color');

    // Close the Chrome view.
    await ermine.threeKeyShortcut(Key.leftCtrl, Key.leftShift, Key.w);
    await ermine.driver.waitUntilNoTransientCallbacks();
    await ermine.waitForAction('close');
    print('Verified that Ermine took CLOSE action');
    expect(await ermine.waitForViewAbsent(chromiumUrl), true);
    print('Closed Chromium');
  }, timeout: Timeout(Duration(minutes: 2)));
}
