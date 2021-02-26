// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:fuchsia_logger/logger.dart' as logger;
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';
import 'package:webdriver/sync_io.dart' show By;

import 'ermine_driver.dart';

const _timeoutSeconds = 10;
const _timeout = Duration(seconds: _timeoutSeconds);
const _sampleViewRect = Rectangle(100, 200, 100, 100);
const testserverUrl =
    'fuchsia-pkg://fuchsia.com/ermine_testserver#meta/ermine_testserver.cmx';

void main() {
  Sl4f sl4f;
  ErmineDriver ermine;

  final newTabFinder = find.text('NEW TAB');
  final indexTabFinder = find.text('Localhost');
  final nextTabFinder = find.text('Next Page');
  final popupTabFinder = find.text('Popup Page');
  final videoTabFinder = find.text('Video Test');
  final redTabFinder = find.text('Red Page');
  final greenTabFinder = find.text('Green Page');
  final blueTabFinder = find.text('Blue Page');

  setUpAll(() async {
    logger.setupLogger(name: 'simple_browser_e2e_test');
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();

    // Starts hosting a local http website.
    // ignore: unawaited_futures
    ermine.component.launch(testserverUrl);
  });

  tearDownAll(() async {
    // Closes the test server.
    // simple-browser is launched via [Component.launch()] since it does not
    // have a view. Therefore, it cannot be closed with ermine's flutter driver.
    // For this reason, we have to explicitly stop the http server to avoid
    // HttpException which occurs in case the test is torn down still having it
    // running.
    // TODO(fxb/69291): Remove this workaround once we can properly close hidden
    // components
    FlutterDriver browser = await ermine.launchAndWaitForSimpleBrowser();
    await browser.requestData('http://127.0.0.1:8080/stop');

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));

    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    expect(await ermine.isStopped(testserverUrl), isTrue);

    await ermine.tearDown();

    await sl4f.stopServer();
    sl4f?.close();
  });

  // TODO(fxb/68689): Transition physical interactions to use Sl4f.Input once
  // fxb/69277 is fixed.
  test('Should be able to do page and history navigation.', () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

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

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
  });

  test('Should be able to play videos on web pages.', () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

    // Access to video.html where the following video is played:
    // experiences/bin/ermine_testserver/public/simple_browser_test/sample_video.mp4
    // It shows the violet-colored background for the first 3 seconds then shows
    // the fuchsia-colored background for another 3 seconds.
    await browser.requestData('http://127.0.0.1:8080/video.html');

    await browser.waitFor(videoTabFinder, timeout: _timeout);

    expect(await browser.getText(videoTabFinder), isNotNull);

    // Waits for a while for the video to be loaded before taking a screenshot.
    await Future.delayed(Duration(seconds: 2));
    final earlyScreenshot = await ermine.screenshot(_sampleViewRect);

    // Takes another screenshot after 3 seconds.
    await Future.delayed(Duration(seconds: 3));
    final lateScreenshot = await ermine.screenshot(_sampleViewRect);

    final diff = ermine.screenshotsDiff(earlyScreenshot, lateScreenshot);
    expect(diff, 1, reason: 'The screenshots are more similar than expected.');

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
  });

  test('Should be able to switch, rearrange, and close tabs', () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

    /// Tab Switching Test

    // TODO(fxb/69334): Get rid of the space in the hint text.
    const newTabHintText = '     SEARCH';
    const redUrl = 'http://127.0.0.1:8080/red.html';
    const greenUrl = 'http://127.0.0.1:8080/green.html';
    const blueUrl = 'http://127.0.0.1:8080/blue.html';

    // Opens red.html in the second tab leaving the first tab as an empty tab.
    await browser.requestData(redUrl);
    await browser.waitFor(redTabFinder, timeout: _timeout);

    // Opens green.html in the third tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(newTabHintText), timeout: _timeout);
    await browser.requestData(greenUrl);
    await browser.waitFor(greenTabFinder, timeout: _timeout);

    // Opens blue.html in the forth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(newTabHintText), timeout: _timeout);
    await browser.requestData(blueUrl);
    await browser.waitFor(blueTabFinder, timeout: _timeout);

    // Should have 4 tabs and the forth tab should be focused.
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(redTabFinder), isNotNull);
    expect(await browser.getText(greenTabFinder), isNotNull);
    expect(await browser.getText(blueTabFinder), isNotNull);
    expect(await browser.getText(find.text(blueUrl)), isNotNull);

    // The second tab should be focused when tapped.
    await browser.tap(redTabFinder);
    await browser.waitFor(find.text(redUrl));
    expect(await browser.getText(find.text(redUrl)), isNotNull);

    // The thrid tab should be focused when tapped.
    await browser.tap(greenTabFinder);
    await browser.waitFor(find.text(greenUrl));
    expect(await browser.getText(find.text(greenUrl)), isNotNull);

    /// Tab Rearranging Test

    // The current order of tabs before rearranging tabs.
    var newTabX = (await browser.getCenter(newTabFinder)).dx;
    var redTabX = (await browser.getCenter(redTabFinder)).dx;
    var greenTabX = (await browser.getCenter(greenTabFinder)).dx;
    var blueTabX = (await browser.getCenter(blueTabFinder)).dx;

    expect(newTabX < redTabX, isTrue,
        reason: 'The NEW TAB is not on the left side of the Red Page tab:'
            'NEW TAB\'s x is $newTabX, Red Page tab\'s X is $redTabX ');
    expect(redTabX < greenTabX, isTrue,
        reason: 'The Red Page tab is not on the left side of the Green Page tab'
            'Red Page tab\'s x is $redTabX, Green Page tab\'s X is $greenTabX ');
    expect(greenTabX < blueTabX, isTrue,
        reason:
            'The Green Page tab is not on the left side of the Blue Page tab'
            'Green Page tab\'s x is $greenTabX, Blue Page tab\'s X is $blueTabX ');

    // Drags the second tab to the right end of the tab list.
    await browser.scroll(redTabFinder, 600, 0, Duration(seconds: 1));

    // The order of tabs after rearranging tabs.
    newTabX = (await browser.getCenter(newTabFinder)).dx;
    redTabX = (await browser.getCenter(redTabFinder)).dx;
    greenTabX = (await browser.getCenter(greenTabFinder)).dx;
    blueTabX = (await browser.getCenter(blueTabFinder)).dx;

    expect(newTabX < greenTabX, isTrue,
        reason: 'The NEW TAB is not on the left side of the Green Page tab.'
            'NEW TAB\'s x is $newTabX, Green Page tab\'s X is $greenTabX ');
    expect(greenTabX < blueTabX, isTrue,
        reason:
            'The Green Page tab is not on the left side of the Blue Page tab'
            'Green Page\'s x is $greenTabX, Blue Page tab\'s X is $blueTabX ');
    expect(blueTabX < redTabX, isTrue,
        reason: 'The Blue Page tab is not on the left side of the Red Page tab'
            'Blue Page tab\'s x is $blueTabX, Red Page tab\'s X is $redTabX ');

    /// Tab closing test
    final tabCloseFinder = find.byValueKey('tab_close');
    await browser.tap(tabCloseFinder);

    // The red page should be gone and the last tab should be focused.
    await browser.waitForAbsent(redTabFinder);

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(greenTabFinder), isNotNull);
    expect(await browser.getText(blueTabFinder), isNotNull);
    expect(await browser.getText(find.text(blueUrl)), isNotNull);

    // TODO(fxb/70265): Test closing an unfocused tab once fxb/68689 is done.

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
  });

  // TODO(fxb/68720): Test web editing
  // TODO(fxb/68716): Test audio playing
}
