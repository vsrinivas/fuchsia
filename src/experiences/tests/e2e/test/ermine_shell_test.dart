// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:ermine_driver/ermine_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

void main() {
  late Sl4f sl4f;
  late ErmineDriver ermine;
  late Input input;

  const kTransientWait = Duration(seconds: 2);

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
    await sl4f.stopServer();
    sl4f.close();
  });

  test('Launch terminal and show recents', () async {
    // Launch terminal.
    const componentUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx';
    await ermine.launch(componentUrl);

    // Terminal should fill the view from left to right edge.
    var viewRect = await ermine.getViewRect(componentUrl);
    expect(viewRect.left, equals(0));

    // Toggle Recents, until it is visible. Terminal view is shifted right.
    expect(
        await ermine.waitFor(() async {
          await ermine.driver.requestData('recents');
          await ermine.driver
              .waitUntilNoTransientCallbacks(timeout: kTransientWait);
          viewRect = await ermine.getViewRect(componentUrl);
          return viewRect.left > 0;
        }),
        isTrue);

    // Close terminal.
    await ermine.driver.requestData('close');
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: kTransientWait);
    await ermine.isStopped(componentUrl);
  });

  // TODO(https://fxbug.dev/73501): Enable once tap gesture is implemented.
  test('Switch focus using pointer', () async {
    // Launch terminal.
    const terminalUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx';
    await ermine.launch(terminalUrl);
    await ermine.waitForView(terminalUrl);

    // Launch spinning_square_view, it should have focus.
    const spinningSquareViewUrl =
        'fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx';
    await ermine.launch(spinningSquareViewUrl);
    var view = await ermine.waitForView(spinningSquareViewUrl);
    expect(view!['focused'], isTrue);

    // Tap on terminal to switch focus to it. Terminal view should be left half
    // of the screen. [input.tap] assumes screen resolution as 1000 x 1000.
    await input.tap(Point(250, 500));
    await ermine.driver.waitUntilNoTransientCallbacks();

    // Terminal should now have focus.
    view = await ermine.waitForView(terminalUrl);
    expect(view!['focused'], isTrue);
  }, skip: true);
}
