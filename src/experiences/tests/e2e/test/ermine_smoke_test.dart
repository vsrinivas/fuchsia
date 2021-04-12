// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_driver/ermine_driver.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - Connect to ermine using Flutter Driver.
///  - Ensure its screenshot is not all black.
void main() {
  Sl4f sl4f;
  ErmineDriver ermine;
  Input input;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();

    input = Input(sl4f);
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
  }, skip: true);

  test('Text input should work', () async {
    await ermine.gotoOverview();

    // Clear the contents of the Ask bar.
    await ermine.driver.requestData('clear');
    await ermine.driver.waitUntilNoTransientCallbacks();

    // Inject text 'spinning_square_view'.
    await input.text('spinning_square_view');

    // Verify text was injected into flutter widgets.
    await ermine.driver.waitUntilNoTransientCallbacks();
    await ermine.driver.waitFor(find.text('spinning_square_view'));
    final askResult = await ermine.driver.getText(find.descendant(
      of: find.byType('AskTextField'),
      matching: find.text('spinning_square_view'),
    ));
    expect(askResult, 'spinning_square_view');
  });
}
