// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_driver/ermine_driver.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

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

  test('verify channel setting present in quicksettings', () async {
    await ermine.gotoOverview();

    // channel will always be present in quicksettings
    final channelTitle = await ermine.driver.getText(find.text('CHANNEL'));
    expect(channelTitle, 'CHANNEL');
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

  test('verify can tap restart in quicksettings', () async {
    await ermine.gotoOverview();

    final button = find.text('RESTART');
    await ermine.driver.waitFor(button);

    // tap restart button to trigger restart
    await ermine.driver.tap(button);
    await Future.delayed(Duration(seconds: 1));

    // wait for system to reboot and reconnect
    // logic taken from `sl4f.reboot()`
    await sl4f.stopServer();
    await Future.delayed(Duration(seconds: 3));

    // try to restart SL4F
    await sl4f.startServer();
    expect(await sl4f.isRunning(), isTrue);
  }, timeout: Timeout(Duration(minutes: 2)));
}
