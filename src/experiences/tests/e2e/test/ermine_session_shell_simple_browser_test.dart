// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';
import 'package:webdriver/sync_io.dart' show By;

import 'ermine_driver.dart';

const _timeoutSeconds = 10;
const _timeout = Duration(milliseconds: _timeoutSeconds * 1000);
const homePageTitle = 'Fuchsia';

void main() {
  Sl4f sl4f;
  ErmineDriver ermine;

  setUp(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();
  });

  tearDownAll(() async {
    await ermine.tearDown();

    await sl4f?.stopServer();
    sl4f?.close();
  });

  test('Run simple browser through Ermine session shell.', () async {
    final browser = await ermine.launchAndWaitForSimpleBrowser();

    final addTab = find.byValueKey('new_tab');
    await browser.waitFor(addTab);

    await browser.tap(addTab);

    final newTab = find.text('NEW TAB');
    await browser.waitFor(newTab, timeout: _timeout);

    // TODO(fxb/35834): Replace fuchsia.dev with a locally hosted website.
    await browser.requestData('fuchsia.dev');
    await browser.waitFor(find.text(homePageTitle), timeout: _timeout);
    final tabTitle = await browser.getText(find.text(homePageTitle));
    expect(tabTitle, homePageTitle);

    final webdriver = await ermine.getWebDriverFor('fuchsia.dev');

    final termsLink = webdriver.findElement(By.linkText('Terms'));
    expect(termsLink, isNotNull, reason: 'Cannot find text link "Terms".');
  });
}
