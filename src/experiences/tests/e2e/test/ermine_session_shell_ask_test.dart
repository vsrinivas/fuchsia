// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// import 'dart:convert';

import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'ermine_driver.dart';

void main() {
  Sl4f sl4f;
  ErmineDriver ermine;

  setUp(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();
  });

  tearDown(() async {
    // Any of these may end up being null if the test fails in setup.
    await ermine.tearDown();
    await sl4f?.stopServer();
    sl4f?.close();
  });

  test('use ask to resolve spinning_square_view using FlutterDriver', () async {
    await ermine.gotoOverview();
    await ermine.driver.enterText('spinning');
    await ermine.driver.waitFor(find.text('spinning_square_view'));
    final askResult =
        await ermine.driver.getText(find.text('spinning_square_view'));
    expect(askResult, 'spinning_square_view');
  });

  test('use ask to launch terminal and verify focus', () async {
    await ermine.gotoOverview();
    await ermine.driver.enterText('');
    final terminalResult = find.text('terminal');
    await ermine.driver.tap(terminalResult);

    // The terminal view should be displayed in a window with title.
    final terminalTitle = await ermine.driver.getText(find.text('terminal'));
    expect(terminalTitle, 'terminal');

    // The inspect data should show that the view has focus.
    const componentUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx';
    final inspect = await ermine.waitForView(componentUrl);
    expect(inspect['focused'], isTrue);

    // Close the terminal view.
    await ermine.driver.requestData('close');
  });

  // TODO(http://fxbug.dev/60790): Implement this when input tool is ready.
  test('use ask to resolve spinning_square_view using input tool', () async {},
      skip: true);
}
