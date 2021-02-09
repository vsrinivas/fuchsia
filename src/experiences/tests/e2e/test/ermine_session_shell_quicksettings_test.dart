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

  tearDownAll(() async {
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

  test('verify can change timezone setting in quicksettings', () async {
    await ermine.gotoOverview();

    // Change the system timezone using setui_client.
    await ermine.component.launch(
        'fuchsia-pkg://fuchsia.com/setui_client#meta/setui_client.cmx',
        ['intl', '--time_zone', 'UTC']);

    // tap default timezone (UTC) to launch timezone list
    final defaultTimezone = find.text('UTC');
    await ermine.driver.waitFor(find.text('UTC'));
    await ermine.driver.tap(defaultTimezone);

    // select America/Los_Angeles timezone from timezone list
    final newTimezone = find.text('America/Los_Angeles');
    await ermine.driver.waitFor(newTimezone);
    await ermine.driver.tap(newTimezone);

    // verify selected timezone is present in quicksettings
    final selectedTimezone =
        await ermine.driver.getText(find.text('AMERICA/LOS_ANGELES'));
    expect(selectedTimezone, 'AMERICA/LOS_ANGELES');
  });
}
