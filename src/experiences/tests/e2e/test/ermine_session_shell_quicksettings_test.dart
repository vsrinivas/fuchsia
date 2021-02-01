// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'ermine_driver.dart';

/// Tests that the DUT running ermine can do the following:
///  - Verify quickstatus memory setting is present
void main() {
  Sl4f sl4f;
  ErmineDriver ermine;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();
  });

  tearDown(() async {
    // Any of these may end up being null if the test fails in setup
    await ermine.tearDown();
    await sl4f?.stopServer();
    sl4f?.close();
  });

  test('verify memory setting present in quicksettings', () async {
    await ermine.gotoOverview();

    // memory will always be present in quicksettings
    final memoryTitle = await ermine.driver.getText(find.text('MEMORY'));
    expect(memoryTitle, 'MEMORY');
  });
}
