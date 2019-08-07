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
  int ermineLightGray;

  setUp(() async {
    sl4fDriver = Sl4f.fromEnvironment();
    ermineBlack = Color.fromRgb(0, 0, 0);
    ermineGray = Color.fromRgb(48, 48, 48);
    ermineWhite = Color.fromRgb(255, 255, 255);
    ermineLightGray = Color.fromRgb(158, 158, 158);
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

    // Get state of screen before launching status menu
    // through histrogram of (color => number of pixels)
    Image image = await Scenic(sl4fDriver).takeScreenshot();
    final Map<int, int> histogram = {};
    for (int i = 0; i < image.length; i += 4) {
      final color = image[i];
      histogram[color] = (histogram[color] ?? 0) + 1;
    }

    // Store initial distribution of central colors for test
    final initialErmineWhitePixels = histogram[ermineWhite] ?? 0;
    final initialErmineBlackPixels = histogram[ermineBlack] ?? 0;
    final initialErmineGrayPixels = histogram[ermineGray] ?? 0;
    final initialErmineLightGrayPixels = histogram[ermineLightGray] ?? 0;

    // Inject 'F5' to trigger launching status.
    await sl4fDriver.ssh.run('input keyevent 62');

    // allow time for status startup
    await Future.delayed(Duration(seconds: 1));

    // Get state of screen after launching status menu
    // through histogram of (color => number of pixels)
    image = await Scenic(sl4fDriver).takeScreenshot();
    histogram.clear();
    for (int i = 0; i < image.length; i += 4) {
      final color = image[i];
      histogram[color] = (histogram[color] ?? 0) + 1;
    }

    // Store updated distribution of central colors for test
    final finalErmineWhitePixels = histogram[ermineWhite] ?? 0;
    final finalErmineBlackPixels = histogram[ermineBlack] ?? 0;
    final finalErmineGrayPixels = histogram[ermineGray] ?? 0;
    final finalErmineLightGrayPixels = histogram[ermineLightGray] ?? 0;

    // Test to see if color distribution of pixels changed,
    // which confirms status menu launch was successful
    expect(initialErmineWhitePixels, lessThan(finalErmineWhitePixels));
    expect(initialErmineBlackPixels, lessThan(finalErmineBlackPixels));
    expect(initialErmineGrayPixels, greaterThan(finalErmineGrayPixels));
    expect(initialErmineLightGrayPixels, lessThan(finalErmineLightGrayPixels));

    // Logout for next test
    await sl4fDriver.ssh.run('input keyevent 63');

    return;
  });
}
