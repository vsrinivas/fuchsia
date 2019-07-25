// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';
import 'package:logging/logging.dart';

final _log = Logger('ermine_e2e_ask_test');

/// Tests that the DUT running ermine can do the following:
///  - login as guest (unauthenticated login)
///  - show the ask_bar
///  - execute commands via the ask_bar
void main() {
  Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('ermine session shell guest login', () async {
    // TODO: note that 'user picker' screen is changing/going away
    // so here we use sessionctl to do guest login. Once final
    // OOBE UI is established for Ermine, this could be revisited
    // so as to provide for more robust testing of no-auth login.
    final result = await sl4fDriver.ssh.run('sessionctl login_guest');

    if (result.exitCode != 0) {
      fail('unable to login guest - check user already logged in?');
    }

    // allow time for shell to startup and ask bar to show
    await Future.delayed(Duration(seconds: 5));

    // verify that ask bar is active
    bool askBarActive = await _checkActiveComponent(
        driver: sl4fDriver, name: 'ermine_ask_module');

    if (!askBarActive) {
      fail('ask bar is not active');
    }

    // Ask bar should have focus at this point, so we should
    // be able to inject text entry via the shell and have it
    // enter the bar.

    // Note sl4f library 'input' support doesn't currently support
    // text or keyevents. In addition to that, input keyevent doesn't
    // appear to support keyevents with modifiers (otherwise we could
    // tie into the show/hide of the askbar and test with esc/ALT+space)

    // As it is, here we'll just open simple browser so that we can
    // verify launching chromium from the ask bar
    await sl4fDriver.ssh.run('input text simple_browser');

    // Have to drive ask bar from suggestion (can't just input
    // 'simple_browser' text and enter). So give the suggestions
    // a second to settle down.
    await Future.delayed(Duration(seconds: 5));

    // with the top suggestion being 'open simple_browser', we can
    // now just inject 'enter' to trigger the action.
    await sl4fDriver.ssh.run('input keyevent 40');

    // allow time for simple_browser to startup
    await Future.delayed(Duration(seconds: 10));

    // verify that simple_browser is active
    bool browserActive =
        await _checkActiveComponent(driver: sl4fDriver, name: 'simple_browser');

    if (!browserActive) {
      fail('simple_browser is not active');
    }

    return;
  });
}

/// checks to see if the component associated with [name] is running.
Future<bool> _checkActiveComponent({Sl4f driver, String name}) async {
  Future<String> getComponents() async {
    // Note: using 'cs' here as iquery doesn't appear to actually provide a
    // list of these active components via the hub.
    final process = await driver.ssh.start('cs');
    final exitCode = await process.exitCode;
    final stdout = await process.stdout.transform(utf8.decoder).join();
    if (exitCode != 0) {
      final stderr = await process.stderr.transform(utf8.decoder).join();
      _log.severe('cs failed with exit code $exitCode');
      if (stdout.isNotEmpty) {
        _log.severe('cs stdout: $stdout');
      }
      if (stderr.isNotEmpty) {
        _log.severe('cs stderr: $stderr');
      }
      return null;
    }
    return stdout;
  }

  final activeComponents = await getComponents();

  if (activeComponents == null) {
    return false;
  }

  final matchingActiveComponentList =
      activeComponents.split('\n').where((line) {
    return line.contains(name);
  }).toList();

  return matchingActiveComponentList.isNotEmpty;
}
