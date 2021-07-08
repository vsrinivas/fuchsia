// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_driver/ermine_driver.dart';
import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:fidl_fuchsia_ui_input3/fidl_async.dart' hide KeyEvent;
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - Respond to keyboard shortcuts to control Ermine.
/// Assumes the shortcuts listed in:
/// http://cs/fuchsia/src/experiences/session_shells/ermine/shell/config/keyboard_shortcuts.json
void main() {
  late Sl4f sl4f;
  late ErmineDriver ermine;
  late Input input;

  const keyPressedDuration = Duration(milliseconds: 100);
  const keyReleasedDuration = Duration(milliseconds: 200);
  const keyExecution = Duration(seconds: 2);

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

  Future<void> launchTerminal() async {
    await ermine.launch(terminalUrl);
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);
    // Verify terminal is launched and has focus.
    expect(
        await ermine.waitFor(() async {
          final view = await ermine.waitForView(terminalUrl);
          return view.focused == true;
        }),
        isTrue);
    // Verify terminal is displaying a prompt.
    expect(
        await ermine.waitFor(() async {
          final snapshot = await Inspect(sl4f).snapshotRoot('terminal.cmx');
          if (snapshot != null) {
            return snapshot['grid'].toString().endsWith('\$');
          }
          return false;
        }),
        isTrue);
    await ermine.driver.waitForAbsent(find.byValueKey('overview'));
  }

  test('Toggle between Overview and Home screens', () async {
    // Launch terminal. This should display Home screen with terminal view.
    await launchTerminal();

    // Shortcut Meta + Esc should toggle to Overview screen.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.escape);
    await ermine.driver.waitFor(find.byValueKey('overview'));
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);

    // Shortcut Meta + Esc should toggle back to Home screen.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.escape);
    await ermine.driver.waitForAbsent(find.byValueKey('overview'));
  });

  test('Toggle Ask using shortcut', () async {
    // Launch terminal. This should display Home screen with terminal view.
    await launchTerminal();
    await ermine.driver.waitForAbsent(find.byType('Ask'));

    // Shortcut Alt + Space should display Ask box.
    await ermine.twoKeyShortcut(Key.leftAlt, Key.space);
    await ermine.driver.waitFor(find.byType('Ask'));
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);

    // Shortcut Alt + Space should hide Ask box.
    await ermine.twoKeyShortcut(Key.leftAlt, Key.space);
    await ermine.driver.waitForAbsent(find.byType('Ask'));
  });

  test('Toggle QuickSettings using shortcut', () async {
    // Launch terminal. This should display Home screen with terminal view.
    await launchTerminal();
    await ermine.driver.waitForAbsent(find.byType('Status'));

    // Shortcut Alt + s should display Quick Settings.
    await ermine.twoKeyShortcut(Key.leftAlt, Key.s);
    await ermine.driver.waitFor(find.byType('Status'));
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);

    // Shortcut Escape should hide QuickSettings.
    await input.keyEvents([
      KeyEvent(Key.escape, keyPressedDuration, KeyEventType.pressed),
      KeyEvent(Key.escape, keyReleasedDuration, KeyEventType.released),
    ]);
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);
    await ermine.driver.waitForAbsent(find.byType('Status'));
  });

  test('Show keyboard help', () async {
    // Launch terminal. This should display Home screen with terminal view.
    await launchTerminal();
    await ermine.driver.waitForAbsent(find.byValueKey('keyboardHelp'));

    // Shortcut Meta + / should display keyboard help.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.slash);
    await ermine.driver.waitFor(find.byValueKey('keyboardHelp'));
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);

    // Shortcut Escape should hide keyboard help.
    await input.keyEvents([
      KeyEvent(Key.escape, keyPressedDuration, KeyEventType.pressed),
      KeyEvent(Key.escape, keyReleasedDuration, KeyEventType.released),
    ]);
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);
    await ermine.driver.waitForAbsent(find.byValueKey('keyboardHelp'));
  });

  test('Toggle Recents', () async {
    // Launch terminal. This should display Home screen with terminal view.
    await launchTerminal();
    await ermine.driver.waitForAbsent(find.byType('Thumbnails'));

    // Shortcut Meta + r should display Recents.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.r);
    await ermine.driver.waitFor(find.byType('Thumbnails'));
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);

    // Shortcut Meta + r should now hide (toggle) Recents.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.r);
    await ermine.driver.waitForAbsent(find.byType('Thumbnails'));
  });

  test('Toggle Fullscreen', () async {
    // Launch terminal. This should display Home screen with terminal view.
    await launchTerminal();
    await ermine.driver.waitForAbsent(find.byValueKey('fullscreen'));

    // Shortcut Meta + f should display terminal fullscreen.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.f);
    await ermine.driver.waitFor(find.byValueKey('fullscreen'));
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);

    // Shortcut Meta + f should restore(toggle) fullscreen.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.f);
    await ermine.driver.waitForAbsent(find.byValueKey('fullscreen'));
  });

  test('Close View', () async {
    // Launch terminal. This should display Home screen with terminal view.
    await launchTerminal();
    await ermine.driver.waitFor(find.text('terminal.cmx'));

    // Shortcut Meta + w should close terminal view and display Overview.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.w);
    await ermine.driver.waitFor(find.byValueKey('overview'));
    await ermine.driver.waitForAbsent(find.text('terminal.cmx'));
  });

  test('Switch workspaces', () async {
    // Launch terminal. This should display Home screen with terminal view.
    await launchTerminal();
    await ermine.driver.waitFor(find.text('terminal.cmx'));

    // Shortcut Meta + right should switch to new workspace on right.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.right);
    await ermine.driver.waitForAbsent(find.text('terminal.cmx'));
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: keyExecution);

    // Shortcut Meta + left should switch back to previous workspace on left.
    await ermine.twoKeyShortcut(Key.leftMeta, Key.left);
    await ermine.driver.waitFor(find.text('terminal.cmx'));
  });
}
