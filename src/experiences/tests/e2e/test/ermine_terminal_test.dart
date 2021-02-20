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
  // TODO(http://fxbug.dev/69242): Uncomment once text injection is fixed.
  //Input input;

  // USB HID code for ENTER key.
  // See <https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf>
  // TODO(http://fxbug.dev/69242): Uncomment once text injection is fixed.
  //const kEnterKey = 40;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();

    // TODO(http://fxbug.dev/69242): Uncomment once text injection is fixed.
    // input = Input(sl4f);
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

  Future<bool> _waitForInstances(String componentUrl, int instances,
      {Duration timeout = const Duration(seconds: 30)}) async {
    return ermine.waitFor(() async {
      var components = await ermine.component.list();
      return components.where((e) => e.contains(componentUrl)).length ==
          instances;
    }, timeout: timeout);
  }

  Future<List<Map<String, dynamic>>> _waitForViews(
      String componentUrl, int instances,
      {Duration timeout = const Duration(seconds: 30)}) async {
    return ermine.waitFor(() async {
      var views = (await ermine.launchedViews())
          .where((view) => view['url'] == componentUrl)
          .toList();
      if (views.length == instances) {
        return views;
      }
      return null;
    }, timeout: timeout);
  }

  test('Launch and close three terminal instances', () async {
    // Launch three instances of component.
    const componentUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx';
    await ermine.launch(componentUrl);
    await ermine.launch(componentUrl);
    await ermine.launch(componentUrl);
    expect(await _waitForInstances(componentUrl, 3), isTrue);

    // Close first instance using keyboard shortcut.
    // TODO(http://fxb/66076): Replace action with shortcut when implemented.
    await ermine.driver.requestData('close');
    var views = await _waitForViews(componentUrl, 2);
    expect(views.last['focused'], isTrue);
    expect(await _waitForInstances(componentUrl, 2), isTrue);

    // Close second instance clicking the close button on view title bar.
    await ermine.driver.tap(find.byValueKey('close'));
    views = await _waitForViews(componentUrl, 1);
    expect(views.last['focused'], isTrue);
    expect(await _waitForInstances(componentUrl, 1), isTrue);

    // Close the third instance by injecting 'exit\n'.
    // TODO(http://fxbug.dev/69242): Uncomment once text injection is fixed.
    // await Future.delayed(Duration(seconds: 1));
    // await input.text('exit');
    // await input.keyPress(kEnterKey);
    // await Future.delayed(Duration(seconds: 1));

    // views = await ermine.launchedViews();
    // terminalViews = views.where((view) => view['url'] == componentUrl);
    // expect(terminalViews.length, 0);
    // expect(await _waitForInstances(componentUrl, 0), isTrue);
  });
}
