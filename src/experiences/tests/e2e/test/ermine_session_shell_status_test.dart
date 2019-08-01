// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:image/image.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - login as guest (unauthenticated login)
///  - show the status menu
void main() {
  Sl4f sl4fDriver;
  int ermineBlack;
  int ermineGray;
  int ermineWhite;

  setUp(() async {
    sl4fDriver = Sl4f.fromEnvironment();
    ermineBlack = Color.fromRgb(0, 0, 0);
    ermineGray = Color.fromRgb(48, 48, 48);
    ermineWhite = Color.fromRgb(255, 255, 255);
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
    final result = await sl4fDriver.ssh.run('sessionctl login_guest');

    if (result.exitCode != 0) {
      fail('unable to login guest - check user already logged in?');
    }

    // allow time for shell to startup
    await Future.delayed(Duration(seconds: 5));

    // Note sl4f library 'input' support doesn't currently support
    // text or keyevents. In addition to that, input keyevent doesn't
    // appear to support keyevents with modifiers (otherwise we could
    // tie into the show/hide of the askbar and test with esc/ALT+space)

    // Dismiss ask menu before screenshot test by injecting 'esc'
    await sl4fDriver.ssh.run('input keyevent 41');

    // allow time for ask to dismiss
    await Future.delayed(Duration(seconds: 1));

    // Confirm screen is grey/blank before launching status menu.
    Image image = await Scenic(sl4fDriver).takeScreenshot();
    expect(image.getPixel(image.width - 20, 20), ermineGray);
    expect(image.getPixel(image.width - 250, 20), ermineGray);
    expect(image.getPixel(image.width - 40, 40), ermineGray);
    expect(image.getPixel(image.width - 400, 40), ermineGray);

    // Inject 'F5' to trigger launching status.
    await sl4fDriver.ssh.run('input keyevent 62');

    // allow time for status startup
    await Future.delayed(Duration(seconds: 1));

    // Confirm status menu has launched via checking if pixel in same
    // location has turned black
    image = await Scenic(sl4fDriver).takeScreenshot();
    expect(image.getPixel(image.width - 20, 20), ermineBlack);
    expect(image.getPixel(image.width - 250, 20), ermineBlack);
    expect(image.getPixel(image.width - 40, 40), ermineWhite);
    expect(image.getPixel(image.width - 400, 40), ermineWhite);

    // Logout for next test
    await sl4fDriver.ssh.run('input keyevent 63');

    return;
  });
}
