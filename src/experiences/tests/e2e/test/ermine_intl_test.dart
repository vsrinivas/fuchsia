// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'ermine_driver.dart';

/// Tests that the DUT running ermine can do the following:
///   - Change locale via setui_client and that change takes effect
void main() {
  Sl4f sl4f;
  SetUi setUi;
  ErmineDriver ermine;

  Future<void> setLocale(String localeId) async {
    await setUi.setLocale(localeId);
  }

  void findTextOnScreen(String text) {
    expect(find.text(text).serialize()['text'], text);
  }

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    setUi = SetUi(sl4f);

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();
  });

  tearDownAll(() async {
    // Restore locale back to 'en-US'.
    await setLocale('en-US');

    await ermine.tearDown();
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
