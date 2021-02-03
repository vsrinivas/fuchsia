// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'ermine_driver.dart';

/// Tests that the DUT running ermine can do the following:
///  - Launch 3 instances of terminal package.
///  - Close them by typing exit, keyboard shortcut and clicking close
///    button on the title bar of the view.
void main() {
  Sl4f sl4f;
  ErmineDriver ermine;
  Input input;

  // USB HID code for ENTER key.
  // See <https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf>
  const kEnterKey = 40;

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

  setUp(() async {
    // Close all running views.
    await ermine.driver.requestData('closeAll');
    await ermine.gotoOverview();
  });

  test('Launch and close three terminal instances', () async {
    // Launch three instances of component.
    const componentUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx';
    await ermine.launch(componentUrl);
    await ermine.launch(componentUrl);
    await ermine.launch(componentUrl);
    var runningComponents = await ermine.component.list();
    expect(runningComponents.where((e) => e.contains(componentUrl)).length, 3);

    // Close first instance using keyboard shortcut.
    // TODO(http://fxb/66076): Replace action with shortcut when implemented.
    await ermine.driver.requestData('close');
    var views = await ermine.launchedViews();
    var terminalViews = views.where((view) => view['url'] == componentUrl);
    expect(terminalViews.length, 2);
    expect(terminalViews.last['focused'], isTrue);
    runningComponents = await ermine.component.list();
    expect(runningComponents.where((e) => e.contains(componentUrl)).length, 2);

    // Close second instance clicking the close button on view title bar.
    await ermine.driver.tap(find.byValueKey('close'));
    views = await ermine.launchedViews();
    terminalViews = views.where((view) => view['url'] == componentUrl);
    expect(terminalViews.length, 1);
    expect(terminalViews.last['focused'], isTrue);
    runningComponents = await ermine.component.list();
    expect(runningComponents.where((e) => e.contains(componentUrl)).length, 1);

    // Close the third instance by injecting 'exit\n'.
    await Future.delayed(Duration(seconds: 1));
    await input.text('exit');
    await input.keyPress(kEnterKey);
    await Future.delayed(Duration(seconds: 1));

    views = await ermine.launchedViews();
    terminalViews = views.where((view) => view['url'] == componentUrl);
    expect(terminalViews.length, 0);
    runningComponents = await ermine.component.list();
    expect(runningComponents.where((e) => e.contains(componentUrl)).length, 0);
  });
}
