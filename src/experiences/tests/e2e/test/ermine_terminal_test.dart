// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_driver/ermine_driver.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - Launch 3 instances of terminal package.
///  - Close them by typing exit, keyboard shortcut and clicking close
///    button on the title bar of the view.
void main() {
  late Sl4f sl4f;
  late ErmineDriver ermine;
  late Input input;

  const componentUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx';

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

  Future<List<Map<String, dynamic>>?> _waitForViews(
      String componentUrl, int instances,
      {bool testForFocus = false,
      Duration timeout = const Duration(seconds: 30)}) async {
    return ermine.waitFor(() async {
      var views = (await ermine.launchedViews())
          .where((view) => view['url'] == componentUrl)
          .toList();
      if (views.length == instances) {
        if (testForFocus) {
          expect(views.any((view) => view['focused']), isTrue);
        }
        return views;
      }
      return null;
    }, timeout: timeout);
  }

  // Gets the contents of terminal buffer. This assumes only one terminal
  // instance is running.
  Future<String> waitForBuffer() {
    return ermine.waitFor(() async {
      final snapshot = await Inspect(sl4f).snapshotRoot('terminal.cmx');
      return snapshot['grid'].toString();
    });
  }

  // Waits for terminal to display the starting prompt. This assumes only one
  // terminal instance is running.
  Future<bool> waitForPrompt() {
    return ermine.waitFor(() async {
      return (await waitForBuffer()).endsWith('\$');
    });
  }

  // Injects a command into terminal. This assumes only one terminal
  // instance is running.
  Future<bool> inject(String cmd,
      {bool verify = true, Duration delay = Duration.zero}) {
    return ermine.waitFor(() async {
      await input.text(cmd);
      await input.keyPress(kEnterKey);
      // Wait for command to start executing.
      await Future.delayed(delay);
      final snapshot = await Inspect(sl4f).snapshotRoot('terminal.cmx');
      if (snapshot != null && snapshot.containsKey('grid')) {
        return snapshot['grid'].toString().contains(cmd);
      }
      return false;
    });
  }

  test('Launch and close three terminal instances', () async {
    // Launch three instances of component.
    await ermine.launch(componentUrl);
    await ermine.launch(componentUrl);
    await ermine.launch(componentUrl);
    expect(await _waitForInstances(componentUrl, 3), isTrue);
    await _waitForViews(componentUrl, 3, testForFocus: true);

    print('Launched 3 terminal instances');

    // Close first instance by clicking the close button on view title bar.
    await ermine.driver.waitUntilNoTransientCallbacks();
    final close = find.descendant(
        of: find.byType('TileChrome'),
        matching: find.byValueKey('close'),
        firstMatchOnly: true);
    await ermine.driver.waitFor(close);
    await ermine.driver.tap(close);
    await _waitForViews(componentUrl, 2, testForFocus: true);

    print('Closed first instance');

    // Close the second instance using keyboard shortcut.
    // TODO(http://fxb/66076): Replace action with shortcut when implemented.
    await ermine.driver.requestData('close');
    await _waitForViews(componentUrl, 1, testForFocus: true);

    print('Closed second instance');

    // Close the third instance by injecting 'exit\n'.
    await waitForPrompt();
    await input.text('exit');
    await input.keyPress(kEnterKey);
    await Future.delayed(Duration(seconds: 1));
    await _waitForViews(componentUrl, 0);

    print('Closed third instance');
  }, timeout: Timeout(Duration(minutes: 1)));

  test('Ping localhost', () async {
    // Launch three instances of component.
    await ermine.launch(componentUrl);
    expect(await _waitForInstances(componentUrl, 1), isTrue);
    await _waitForViews(componentUrl, 1, testForFocus: true);
    await waitForPrompt();

    // Inject 'ping localhost' + ENTER
    expect(await inject('ping localhost', delay: Duration(seconds: 3)), isTrue);

    // Verify that terminal buffer contains result of ping.
    final result = await waitForBuffer();
    expect(result.contains('33 bytes from localhost'), isTrue);
  });

  test('ls /hub', () async {
    // Launch three instances of component.
    await ermine.launch(componentUrl);
    expect(await _waitForInstances(componentUrl, 1), isTrue);
    await _waitForViews(componentUrl, 1, testForFocus: true);
    await waitForPrompt();

    // Inject 'ls /hub' + ENTER
    expect(await inject('ls /hub', delay: Duration(seconds: 2)), isTrue);

    final result = await waitForBuffer();
    expect(result.contains('job'), isTrue);
    expect(result.contains('svc'), isTrue);
  });

  test('Navigate filesystem: ls, cd, pwd', () async {
    // Launch three instances of component.
    await ermine.launch(componentUrl);
    expect(await _waitForInstances(componentUrl, 1), isTrue);
    await _waitForViews(componentUrl, 1, testForFocus: true);
    await waitForPrompt();

    // Inject 'ls /' + ENTER
    expect(await inject('ls /', delay: Duration(seconds: 2)), isTrue);

    var result = await waitForBuffer();
    expect(result.contains('bin'), isTrue);
    expect(result.contains('boot'), isTrue);

    // Inject 'cd pkg' + ENTER
    expect(await inject('cd /pkg', delay: Duration(seconds: 1)), isTrue);

    // Inject 'pwd' + ENTER
    expect(await inject('pwd', delay: Duration(seconds: 1)), isTrue);

    // Verify that terminal buffer contains result of ping.
    result = await waitForBuffer();
    expect(result.contains('/pkg'), isTrue);
  });

  test('dm reboot', () async {
    // Launch three instances of component.
    await ermine.launch(componentUrl);
    expect(await _waitForInstances(componentUrl, 1), isTrue);
    await _waitForViews(componentUrl, 1, testForFocus: true);
    await waitForPrompt();

    // Inject 'dm reboot' + ENTER
    await input.text('dm reboot');
    await input.keyPress(kEnterKey);
    await Future.delayed(Duration(seconds: 1));

    // Now we wait for the sytem to reboot and reconnect. This logic is taken
    // from `sl4f.reboot()`.
    await sl4f.stopServer();
    await Future.delayed(Duration(seconds: 3));

    // Try to restart SL4F
    await sl4f.startServer();
    expect(await sl4f.isRunning(), isTrue);
  }, timeout: Timeout(Duration(minutes: 2)));
}
