// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_driver/ermine_driver.dart';
import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

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

  test('Text input, pointer input and keyboard shortcut', () async {
    print('Launching terminal...');
    await ermine.enterTextInAsk('terminal', gotoOverview: true);

    print('Verifying terminal in Ask results...');
    // Verify the auto-complete list has terminal in it.
    final terminalFinder = find.descendant(
      of: find.byType('AskSuggestionList'),
      matching: find.text('terminal'),
      firstMatchOnly: true,
    );
    final askResult = await ermine.driver.getText(terminalFinder);
    expect(askResult, 'terminal');

    print('Tap on terminal Ask result');
    // Tap on 'terminal' auto-complete result.
    final center = await ermine.driver.getCenter(terminalFinder);
    await ermine.tap(center);

    print('Verifying terminal view is visible');
    // Check that terminal was launched.
    expect(await ermine.isRunning(terminalUrl), isTrue);
    expect(await ermine.waitForView(terminalUrl), isNotNull);

    print('Tap in the center of terminal to set input focus');
    final terminalCenter = await ermine.driver.getCenter(find.text('terminal'));
    await ermine.tap(terminalCenter);

    print('Verifying Meta+w shortcut is closing terminal');
    // Use keyboard shortcut to close terminal.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.w);
    await ermine.driver.waitUntilNoTransientCallbacks();
    expect(await ermine.isStopped(terminalUrl), isTrue);
  });
}
