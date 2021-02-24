// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'ermine_driver.dart';

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

  test('Launch terminal and show recents', () async {
    // Launch terminal.
    const componentUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx';
    await ermine.launch(componentUrl);
    expect(await ermine.waitForView(componentUrl), isNotNull);

    // Toggle Recents.
    await ermine.driver.requestData('recents');
    // We should have thumbnails for running views.
    final thumbnails = find.descendant(
        of: find.byType('Thumbnails'), matching: find.byType('Text'));
    await ermine.driver.waitFor(thumbnails);
    expect(await ermine.driver.getText(thumbnails), 'terminal.cmx');

    // Close terminal.
    await ermine.driver.requestData('close');
    await ermine.isStopped(componentUrl);
  });
}
