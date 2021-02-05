// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';
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
  FlutterDriver browser;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();

    // Starts hosting a local http website.
    // TODO(fxb/69256): Replace `ermine.launch(testserverUrl)` with
    // `ermine.component.launch(testserverUrl)`
    await ermine.launch(testserverUrl);

    final runningComponents = await ermine.component.list();
    expect(runningComponents.where((c) => c.contains(testserverUrl)).length, 1);

    // Launches a browser.
    browser = await ermine.launchAndWaitForSimpleBrowser();
  });

  tearDownAll(() async {
    // Stops the server.
    await browser.requestData('http://127.0.0.1:8080/stop');

    // Closes all views.
    await ermine.driver.requestData('closeAll');
    await ermine.driver.waitForAbsent(find.text('ermine_testserver.cmx'));
    await ermine.driver.waitForAbsent(find.text('simple_browser'));

    final runningComponents = await ermine.component.list();
    expect(runningComponents.where((c) => c.contains(testserverUrl)).length, 0);
    expect(
        runningComponents.where((c) => c.contains(simpleBrowserUrl)).length, 0);

    await ermine.tearDown();

    await sl4f.stopServer();
    sl4f?.close();
  });

  // TODO(fxb/68689): Transition physical interactions to use Sl4f.Input
  test(
      'Simple browser\'s page and history navigation features '
      'should work correctly.', () async {
    final newTabFinder = find.text('NEW TAB');
    final indexTabFinder = find.text('Localhost');
    final nextTabFinder = find.text('Next Page');
    final popupTabFinder = find.text('Popup Page');

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
  });

  test('Simple browser should be able to play videos on web pages.', () async {
    // Access to video.html where the following video is played:
    // experiences/bin/ermine_testserver/public/simple_browser_test/sample_video.mp4
    // It shows the violet-colored background for the first 3 seconds then shows
    // the fuchsia-colored background for another 3 seconds.
    await browser.requestData('http://127.0.0.1:8080/video.html');

    final videoTabFinder = find.text('Video Test');
    await browser.waitFor(videoTabFinder, timeout: _timeout);

    expect(await browser.getText(videoTabFinder), isNotNull);

    // Takes the screenshot of the part of the video.
    // Only the color is changed in this area as the video is played.
    final viewRect = Rectangle(100, 100, 100, 100);

    // Waits for a while for the video to be loaded before taking a screenshot.
    await Future.delayed(Duration(seconds: 2));
    final earlyScreenshot = await ermine.screenshot(viewRect);

    // Takes another screenshot after 3 seconds.
    await Future.delayed(Duration(seconds: 3));
    final lateScreenshot = await ermine.screenshot(viewRect);

    final diff = ermine.screenshotsDiff(earlyScreenshot, lateScreenshot);
    expect(diff, 1, reason: 'The screenshots are more similar than expected.');
  });
}
