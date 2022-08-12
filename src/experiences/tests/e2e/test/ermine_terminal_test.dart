// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: import_of_legacy_library_into_null_safe

@Retry(2)

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

  const componentUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cm';

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
    await ermine.driver.requestData('closeAll');
  });

  Future<List<ViewSnapshot>> _waitForViews(String componentUrl, int instances,
      {bool testForFocus = false,
      Duration timeout = const Duration(seconds: 30)}) async {
    return ermine.waitFor(() async {
      var views = await ermine.launchedViews(filterByUrl: componentUrl);
      if (views.length == instances) {
        if (testForFocus) {
          // Wait for a view with focus.
          if (!views.any((view) => view.focused)) {
            return null;
          }
        }
        return views;
      }
      return null;
    }, timeout: timeout);
  }

  // Gets the contents of terminal buffer. This assumes only element instance
  // is running, and that element is a terminal.
  Future<String> waitForBuffer() {
    return ermine.waitFor(() async {
      final snapshot = await Inspect(sl4f).snapshotRoot(
          'core/session-manager/session\\:session/workstation_session/login_shell/ermine_shell/elements*');
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
  Future<void> inject(String cmd,
      {Duration delay = const Duration(seconds: 1)}) async {
    await input.text(cmd);
    await Future.delayed(delay);

    // Wait for buffer to contain the injected command.
    await ermine.waitFor(() async {
      final buffer = await waitForBuffer();
      return buffer.contains(cmd);
    });

    // Commit the command.
    await input.keyPress(kEnterKey);
    await Future.delayed(delay);
  }

  test('Launch and close two terminal instances', () async {
    // Launch two instances of component.
    await ermine.launchFromAppLauncher('Terminal');
    await _waitForViews(componentUrl, 1, testForFocus: true);
    await ermine.launchFromAppLauncher('Terminal');
    await _waitForViews(componentUrl, 2, testForFocus: true);

    print('Launched 2 terminal instances');

    // Close the first instance by injecting 'exit\n'.
    await waitForPrompt();
    await inject('exit');
    await ermine.driver.waitUntilNoTransientCallbacks();
    await _waitForViews(componentUrl, 1, testForFocus: true);

    print('Closed first instance');

    // Close the second instance using keyboard shortcut.
    await ermine.threeKeyShortcut(Key.leftCtrl, Key.leftShift, Key.w);
    await ermine.driver.waitUntilNoTransientCallbacks();
    await _waitForViews(componentUrl, 0);

    print('Closed second instance');
  }, timeout: Timeout(Duration(minutes: 1)));

  test('Ping localhost', () async {
    // Launch three instances of component.
    await ermine.launchFromAppLauncher('Terminal');
    await _waitForViews(componentUrl, 1, testForFocus: true);
    await waitForPrompt();

    // Inject 'ping localhost' + ENTER
    await inject('ping localhost', delay: Duration(seconds: 3));

    // Verify that terminal buffer contains result of ping.
    final result = await waitForBuffer();
    print('ping response: $result');
    expect(result, contains('bytes from localhost'));
  });

  test('ls /hub', () async {
    // Launch three instances of component.
    await ermine.launchFromAppLauncher('Terminal');
    await _waitForViews(componentUrl, 1, testForFocus: true);
    await waitForPrompt();

    // Inject 'ls /hub' + ENTER
    await inject('ls /hub', delay: Duration(seconds: 2));

    final result = await waitForBuffer();
    expect(result, contains('job'));
    expect(result, contains('svc'));
  });

  test('Navigate filesystem: ls, cd, pwd', () async {
    // Launch three instances of component.
    await ermine.launchFromAppLauncher('Terminal');
    await _waitForViews(componentUrl, 1, testForFocus: true);
    await waitForPrompt();

    // Inject 'ls /' + ENTER
    await inject('ls /', delay: Duration(seconds: 2));

    var result = await waitForBuffer();
    expect(result, contains('bin'));
    expect(result, contains('boot'));

    // Inject 'cd pkg' + ENTER
    await inject('cd /pkg');

    // Inject 'pwd' + ENTER
    await inject('pwd');

    // Verify that terminal buffer contains result of ping.
    result = await waitForBuffer();
    expect(result, contains('/pkg'));
  });

  test('dm reboot', () async {
    try {
      // Launch three instances of component.
      await ermine.launchFromAppLauncher('Terminal');
      await _waitForViews(componentUrl, 1, testForFocus: true);
      await waitForPrompt();

      // Inject 'dm reboot' + ENTER
      await inject('dm reboot');

      // Now we wait for the sytem to reboot and reconnect. This logic is taken
      // from `sl4f.reboot()`.
      await sl4f.stopServer();
      await Future.delayed(Duration(seconds: 3));

      // Try to restart SL4F
      await sl4f.startServer();
      expect(await sl4f.isRunning(), isTrue);
      //ignore: avoid_catches_without_on_clauses
    } catch (e, s) {
      print('exception: $e');
      print('stack trace: $s');
    }
  }, timeout: Timeout(Duration(minutes: 2)));
}
