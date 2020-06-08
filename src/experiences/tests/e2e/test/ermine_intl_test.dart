// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///   - Change locale via setui_client and that change takes effect
void main() {
  Sl4f sl4f;
  SetUi setUi;
  FlutterDriverConnector connector;
  FlutterDriver driver;

  Future<void> setLocale(String localeId) async {
    await setUi.setLocale(localeId);
  }

  void findTextOnScreen(String text) {
    expect(find.text(text).serialize()['text'], text);
  }

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    setUi = sl4f.SetUi(sl4f);

    connector = FlutterDriverConnector(sl4f);
    await connector.initialize();

    // Check if ermine is running.
    final isolate = await connector.isolate('ermine');
    if (isolate == null) {
      fail('could not find ermine.');
    }

    // Now connect to ermine.
    driver = await connector.driverForIsolate('ermine');
    if (driver == null) {
      fail('unable to connect to ermine.');
    }
  });

  tearDownAll(() async {
    await setLocale('en-US');

    // Any of these may end up being null if the test fails in setup.
    await driver?.close();
    await connector?.tearDown();
    await sl4f?.stopServer();
    sl4f?.close();
  });

  test('Locale can be switched and takes effect', () async {
    await setLocale('en-US');
    findTextOnScreen('MEMORY');

    // The text on screen is equivalent to US English "MEMORY".
    await setLocale('sr');
    findTextOnScreen('МЕМОРИЈА');
  });
}
