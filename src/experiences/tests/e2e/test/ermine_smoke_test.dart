// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: import_of_legacy_library_into_null_safe

@Retry(2)
@Timeout(Duration(minutes: 2))

import 'package:ermine_driver/ermine_driver.dart';
import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - Connect to ermine using Flutter Driver.
///  - Ensure its screenshot is not all black.
void main() {
  late Sl4f sl4f;
  late ErmineDriver ermine;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();
  });

  tearDownAll(() async {
    await ermine.tearDown();
    await sl4f.stopServer();
    sl4f.close();
  });

  test('Screen should not be black', () async {
    // Generate a flutter driver screenshot to ensure the app is ready.
    final bytes = await ermine.driver.screenshot();
    expect(bytes.length, isPositive);

    // Take a screen shot using Scenic and make sure it is not all black.
    final scenic = Scenic(sl4f);
    final image = await scenic.takeScreenshot(dumpName: 'screen_not_black');
    bool isAllBlack = image.data.every((pixel) => pixel & 0x00ffffff == 0);
    expect(isAllBlack, false);
  });

  test('Text input, pointer input and keyboard shortcut', () async {
    print('Launching terminal...');
    final terminalFinder = find.text('Terminal');
    await ermine.driver.waitUntilNoTransientCallbacks();
    final appResult = await ermine.driver.getText(terminalFinder);
    expect(appResult, 'Terminal');

    // Tap on 'Terminal' app launcher entry.
    print('Tap on terminal entry');
    final center = await ermine.driver.getCenter(terminalFinder);
    await ermine.tap(center);

    // Check that terminal was launched.
    print('Verifying terminal view is visible');
    expect(await ermine.isRunning(terminalUrl), isTrue);
    await ermine.driver.waitForAbsent(terminalFinder);

    // Use keyboard shortcut to close terminal.
    print('Verifying Ctrl+Shift+w shortcut is closing terminal');
    await ermine.threeKeyShortcut(Key.leftCtrl, Key.leftShift, Key.w);
    await ermine.driver.waitUntilNoTransientCallbacks();
    await ermine.waitForAction('close');
    await ermine.waitForViewAbsent(terminalUrl);

    // Get the current value of dark mode.
    bool darkMode = (await ermine.snapshot).darkMode;

    // Toggle it.
    await ermine.driver.tap(find.byValueKey('darkMode'));
    expect((await ermine.snapshot).darkMode, !darkMode);

    // Logout from ermine.
    print('Logging out and back in');
    await ermine.logoutAndLogin();

    // Dark mode toggle should have persisted across auth flows.
    expect((await ermine.snapshot).darkMode, !darkMode);
  }, timeout: Timeout(Duration(minutes: 2)));
}
