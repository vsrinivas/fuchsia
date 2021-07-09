// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: import_of_legacy_library_into_null_safe

import 'package:ermine_driver/ermine_driver.dart';
import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - Launch 2 instances of terminal package and close them by typing exit
///  and keyboard shortcut.
///  - ping localhost
///  - ls /hub
///  -  ls, cd, pwd
///  - dm reboot
void main() {
  late Sl4f sl4f;
  late ErmineDriver ermine;
  late Input input;

  const componentUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx';

  setUp(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();

    input = Input(sl4f);
  });

  tearDown(() async {
    // Any of these may end up being null if the test fails in setup.
    await ermine.tearDown();
    await sl4f.stopServer();
    sl4f.close();
  });

  Future<List<ViewSnapshot>> _waitForViews(String componentUrl, int instances,
      {bool testForFocus = false,
      Duration timeout = const Duration(seconds: 30)}) async {
    return ermine.waitFor(() async {
      var views = await ermine.launchedViews(filterByUrl: componentUrl);
      if (views.length == instances) {
        if (testForFocus) {
          expect(views.any((view) => view.focused), isTrue);
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
      final snapshot = await ermine.inspectSnapshot('terminal.cmx');
      return snapshot['grid'];
    });
  }

  // Waits for terminal to display the starting prompt. This assumes only one
  // terminal instance is running.
  Future<bool> waitForPrompt() async {
    return ermine.waitFor(() async {
      final buffer = await waitForBuffer();
      return buffer.endsWith('\$');
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
      final snapshot = await ermine.inspectSnapshot('terminal.cmx');
      if (snapshot.containsKey('grid')) {
        return snapshot['grid'].toString().contains(cmd);
      }
      return false;
    });
  }

  test('Launch and close two terminal instances', () async {
    // Launch two instances of component.
    await ermine.launch(componentUrl);
    await ermine.launch(componentUrl);
    await _waitForViews(componentUrl, 2, testForFocus: true);

    print('Launched 2 terminal instances');

    // Close the first instance using keyboard shortcut.
    await ermine.threeKeyShortcut(Key.leftCtrl, Key.leftShift, Key.w);
    await ermine.driver.waitUntilNoTransientCallbacks();
    await _waitForViews(componentUrl, 1, testForFocus: true);

    print('Closed first instance');

    // Close the second instance by injecting 'exit\n'.
    await waitForPrompt();
    await input.text('exit');
    await input.keyPress(kEnterKey);
    await Future.delayed(Duration(seconds: 1));
    await _waitForViews(componentUrl, 0);

    print('Closed second instance');
  }, timeout: Timeout(Duration(minutes: 1)));

  test('Ping localhost', () async {
    // Launch three instances of component.
    await ermine.launch(componentUrl);
    await _waitForViews(componentUrl, 1, testForFocus: true);
    await waitForPrompt();

    // Inject 'ping localhost' + ENTER
    expect(await inject('ping localhost', delay: Duration(seconds: 3)), isTrue);

    // Verify that terminal buffer contains result of ping.
    final result = await waitForBuffer();
    print('ping response: $result');
    expect(result.contains('bytes from localhost'), isTrue);
  });

  test('ls /hub', () async {
    // Launch three instances of component.
    await ermine.launch(componentUrl);
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
