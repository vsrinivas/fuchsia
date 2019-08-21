// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - login as guest (unauthenticated login)
///  - show the status menu
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

  test('ermine session shell status menu', () async {
    // TODO: note that 'user picker' screen is changing/going away
    // so here we use sessionctl to do guest login. Once final
    // OOBE UI is established for Ermine, this could be revisited
    // so as to provide for more robust testing of no-auth login.
    await sl4fDriver.ssh.run('sessionctl restart_session');

    final result = await sl4fDriver.ssh.run('sessionctl login_guest');

    if (result.exitCode != 0) {
      fail('unable to login guest - check user already logged in?');
    }

    // allow time for shell to startup
    await Future.delayed(Duration(seconds: 3));

    // Note sl4f library 'input' support doesn't currently support
    // text or keyevents. In addition to that, input keyevent doesn't
    // appear to support keyevents with modifiers (otherwise we could
    // tie into the show/hide of the askbar and test with esc/ALT+space)

    // Dismiss ask menu before screenshot test by injecting 'esc'
    await sl4fDriver.ssh.run('input keyevent 41');

    // allow time for ask to dismiss
    await Future.delayed(Duration(seconds: 1));

    // Get state of screen before launching status menu. Status should be
    // missing.
    final inspect = Inspect(sl4fDriver.ssh);
    var json = await inspect.inspectComponentRoot('ermine');
    if (json['status'] != null) {
      fail('Status should not be visible at start');
    }

    // Inject 'F5' to trigger launching status.
    await sl4fDriver.ssh.run('input keyevent 62');

    // allow time for status startup
    await Future.delayed(Duration(seconds: 1));

    json = await inspect.inspectComponentRoot('ermine');
    if (json['status'] == null) {
      fail('Status did not launch');
    }

    // Logout for next test
    await sl4fDriver.ssh.run('input keyevent 63');

    return;
  });
}
