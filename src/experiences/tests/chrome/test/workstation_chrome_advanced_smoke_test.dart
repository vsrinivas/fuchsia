// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: import_of_legacy_library_into_null_safe
import 'dart:math';

import 'package:ermine_driver/ermine_driver.dart';
import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

const chromiumUrl = 'fuchsia-pkg://fuchsia.com/chrome#meta/chrome.cm';
const testserverUrl =
    'fuchsia-pkg://fuchsia.com/ermine_testserver#meta/ermine_testserver.cmx';

void main() {
  late Sl4f sl4f;
  late ErmineDriver ermine;
  late Input input;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();

    input = Input(sl4f);
    print('Set up Input');
  });

  tearDownAll(() async {
    await ermine.tearDown();
    print('Tore down Ermine flutter driver');
    await sl4f.stopServer();
    print('Stopped sl4f server');
    sl4f.close();
    print('Closed sl4f');
  });

  test('Chrome browser should be able to access and render web pages.',
      () async {
    // Launches test server app
    expect(await ermine.launch(testserverUrl), isTrue);
    await ermine.driver.waitUntilNoTransientCallbacks();
    print('Launched the test server.');

    // Calls the app launcher
    // TODO(fxb/94441): Launch Chromium using [ErmineDriver.launch] once the blocker is fixed.
    await ermine.threeKeyShortcut(Key.leftMeta, Key.leftAlt, Key.space);
    await ermine.driver.waitUntilNoTransientCallbacks();
    final ermineSnapshot = await ermine.snapshot;
    expect(ermineSnapshot.sideBarVisible, isTrue);
    print('Opened the app launcher');

    // Launch Chromium app
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

    const blueUrl = 'http://127.0.0.1:8080/blue.html';
    await input.text(blueUrl, keyEventDuration: Duration(milliseconds: 50));
    print('Typed in $blueUrl to the browser');
    await input.keyPress(kEnterKey);
    print('Pressed Enter');

    const blue = 0xffff0000; // (0xAABBGGRR)
    Map<int, int> histogram;

    await Future.delayed(Duration(seconds: 3));
    final isBlue = await ermine.waitFor(() async {
      print('Take a screenshot...');
      final screenshot = await ermine.screenshot(Rectangle(500, 500, 100, 100));
      histogram = ermine.histogram(screenshot);
      print('Color key: ${histogram.keys.first}');
      print('Color value: ${histogram.values.first}');
      if (histogram.keys.length == 1 && histogram[blue] == 10000) {
        return true;
      }
      return false;
    }, timeout: Duration(minutes: 2));

    expect(isBlue, isTrue);
    print('Verified the expected background color');

    // Close Chromium
    print('Closing the Chromium View');
    await ermine.threeKeyShortcut(Key.leftCtrl, Key.leftShift, Key.w);
    await ermine.driver.waitUntilNoTransientCallbacks();
    await ermine.waitForAction('close');
    print('Verified that Ermine took CLOSE action');
    expect(await ermine.waitForViewAbsent(chromiumUrl), true);
    print('Closed Chromium');

    // Close the test server.
    print('Closing the test server');
    await ermine.threeKeyShortcut(Key.leftCtrl, Key.leftShift, Key.w);
    await ermine.driver.waitUntilNoTransientCallbacks();
    await ermine.waitForAction('close');
    print('Verified that Ermine took CLOSE action');
    expect(await ermine.waitForViewAbsent(testserverUrl), true);
    print('Closed test server');
  }, timeout: Timeout(Duration(minutes: 3)));
}
