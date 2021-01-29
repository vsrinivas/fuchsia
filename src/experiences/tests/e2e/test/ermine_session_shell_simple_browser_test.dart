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
const testserverUrl =
    'fuchsia-pkg://fuchsia.com/ermine_testserver#meta/ermine_testserver.cmx';

void main() {
  Sl4f sl4f;
  ErmineDriver ermine;

  setUpAll(() async {
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

  // TODO(fxb/68689): Transition physical interactions to use Sl4f.Input
  test(
      'Simple browser\'s page and history navigation features '
      'should work correctly.', () async {
    // Hosts a local http website.
    await ermine.launch(testserverUrl);

    // Launches a browser.
    final browser = await ermine.launchAndWaitForSimpleBrowser();

    // Adds a new tab.
    final addTab = find.byValueKey('new_tab');
    await browser.waitFor(addTab);

    await browser.tap(addTab);

    final newTabFinder = find.text('NEW TAB');
    final indexTabFinder = find.text('Localhost');
    final nextTabFinder = find.text('Next Page');
    final popupTabFinder = find.text('Popup Page');

    await browser.waitFor(newTabFinder, timeout: _timeout);

    // Access to the website.
    await browser.requestData('http://127.0.0.1:8080/index.html');
    await browser.waitFor(indexTabFinder, timeout: _timeout);
    final webdriver = await ermine.getWebDriverFor('127.0.0.1');

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);

    final nextLink = webdriver.findElement(By.linkText('Next'));
    expect(nextLink, isNotNull);

    // Clicks the text link that opens next.html (page navigation)
    nextLink.click();
    await browser.waitForAbsent(indexTabFinder, timeout: _timeout);
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(nextTabFinder), isNotNull);

    final prevLink = webdriver.findElement(By.linkText('Prev'));
    expect(prevLink, isNotNull);

    // Clicks the text link that opens index.html (page navigation)
    prevLink.click();
    await browser.waitForAbsent(nextTabFinder, timeout: _timeout);
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);

    // Goes back to next.html by tapping the BCK button (history navigation)
    final back = find.byValueKey('back');
    await browser.tap(back);
    await browser.waitForAbsent(indexTabFinder, timeout: _timeout);
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(nextTabFinder), isNotNull);

    // Goes forward to index.html by tapping the FWD button (history navigation)
    final forward = find.byValueKey('forward');
    await browser.tap(forward);
    await browser.waitForAbsent(nextTabFinder, timeout: _timeout);
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);

    // Clicks + button to increase the number
    var digitLink = webdriver.findElement(By.id('target'));
    final addButton = webdriver.findElement(By.id('increase'));
    expect(digitLink.text, '0');
    addButton.click();
    expect(digitLink.text, '1');
    addButton.click();
    expect(digitLink.text, '2');

    // Refreshes the page
    final refresh = find.byValueKey('refresh');
    await browser.tap(refresh);
    digitLink = webdriver.findElement(By.id('target'));
    expect(digitLink.text, '0');

    // Clicks the text link that opens popup.html (popup page navigation)
    final popupLink = webdriver.findElement(By.linkText('Popup'));
    expect(popupLink, isNotNull);

    popupLink.click();
    await browser.waitFor(popupTabFinder, timeout: _timeout);
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    expect(await browser.getText(popupTabFinder), isNotNull);

    // Close the view.
    await ermine.driver.requestData('close');
    // Verify the view is closed.
    await ermine.driver.waitForAbsent(find.text('simple_browser'));

    await ermine.driver.requestData('close');
    // Verify the view is closed.
    await ermine.driver.waitForAbsent(find.text('ermine_testserver.cmx'));
  });
}
