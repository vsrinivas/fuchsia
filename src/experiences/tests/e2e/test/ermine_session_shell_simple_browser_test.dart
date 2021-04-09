// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:ermine_driver/ermine_driver.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';
import 'package:webdriver/sync_io.dart';

const _timeoutOneSec = Duration(seconds: 1);
const _timeoutTenSec = Duration(seconds: 10);
const _sampleViewRect = Rectangle(100, 200, 100, 100);
const testserverUrl =
    'fuchsia-pkg://fuchsia.com/ermine_testserver#meta/ermine_testserver.cmx';

void main() {
  Sl4f sl4f;
  ErmineDriver ermine;
  WebDriverConnector webDriverConnector;
  Input input;

  final newTabFinder = find.text('NEW TAB');
  final indexTabFinder = find.text('Localhost');
  final nextTabFinder = find.text('Next Page');
  final popupTabFinder = find.text('Popup Page');
  final videoTabFinder = find.text('Video Test');
  final redTabFinder = find.text('Red Page');
  final greenTabFinder = find.text('Green Page');
  final blueTabFinder = find.text('Blue Page');

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();

    input = Input(sl4f);

    webDriverConnector = WebDriverConnector('runtime_deps/chromedriver', sl4f);
    await webDriverConnector.initialize();

    // Starts hosting a local http website.
    // ignore: unawaited_futures
    ermine.component.launch(testserverUrl);
    expect(await ermine.isRunning(testserverUrl), isTrue);
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
    const stopUrl = 'http://127.0.0.1:8080/stop';
    await browser.requestData(stopUrl);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitFor(find.text(stopUrl), timeout: _timeoutTenSec);
    expect(await ermine.isStopped(testserverUrl), isTrue);
    print('Stopped the test server');
    await browser.close();

    await ermine.driver.requestData('close');
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    print('Closed the browser');

    await webDriverConnector?.tearDown();
    await ermine.tearDown();
    await sl4f?.stopServer();
    sl4f?.close();
  });

  Future<bool> _waitForTabArrangement(FlutterDriver browser,
      SerializableFinder leftTabFinder, SerializableFinder rightTabFinder,
      {Duration timeout = const Duration(seconds: 30)}) async {
    return ermine.waitFor(() async {
      final leftTabX = (await browser.getCenter(leftTabFinder)).dx;
      final rightTabX = (await browser.getCenter(rightTabFinder)).dx;
      return leftTabX < rightTabX;
    }, timeout: timeout);
  }

  /// Keeps finding a web element that satisfies the given [by] condition until
  /// the timeout expires. [NoSuchElementException] thrown by [findElement]
  /// in the meantime is ignored.
  /// Returns the [WebElement] if it finds one. Otherwise, returns null.
  Future<WebElement> _waitForWebElement(WebDriver web, By by) async {
    return await ermine.waitFor<WebElement>(() async {
      try {
        final element = web.findElement(by);
        return element;
      } on NoSuchElementException {
        return null;
      }
    }, timeout: _timeoutTenSec);
  }

  /// Keeps calling the given action until it gets the expected result within
  /// a fixed time. Errors thrown by the action in the meantime is ignored.
  /// e.g. [DriverError], thrown in case the driver fails to locate a [Finder].
  /// Returns true if it gets the expected result. Otherwise, returns false.
  Future<bool> _repeatActionUntilGetResult(
      void action(), Future<void> result()) async {
    return await ermine.waitFor(() async {
      action.call();
      try {
        await result.call();
        return true;
        // ignore: avoid_catches_without_on_clauses
      } catch (e) {
        print('$e. Keep repeating the action until timeout expires...');
        return false;
      }
    }, timeout: _timeoutTenSec);
  }

  /// Keeps calling `waitFor(finder)` until the timeout expires. Returns true
  /// if it locates one. Otherwise, returns false.
  Future<bool> _repeatActionWaitingFor(
      FlutterDriver browser, void action(), SerializableFinder finder) async {
    return await _repeatActionUntilGetResult(
        action, () => browser.waitFor(finder, timeout: _timeoutOneSec));
  }

  /// Keeps calling `waitForAbsent(finder)` until the timeout expires. Returns
  /// true if it locates one. Otherwise, returns false.
  Future<bool> _repeatActionWaitingForAbsent(
      FlutterDriver browser, void action(), SerializableFinder finder) async {
    return await _repeatActionUntilGetResult(
        action, () => browser.waitForAbsent(finder, timeout: _timeoutOneSec));
  }

  // TODO(fxb/68689): Transition pointer interactions to Sl4f.Input once it is
  // ready.
  test('Should be able to do page and history navigation.', () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

    // Access to the website.
    await input.text('http://127.0.0.1:8080/index.html');
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitFor(indexTabFinder, timeout: _timeoutTenSec);

    final webdriver =
        (await webDriverConnector.webDriversForHost('127.0.0.1')).single;

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    print('Opened http://127.0.0.1:8080/index.html');

    final nextLink = await _waitForWebElement(webdriver, By.linkText('Next'));
    expect(nextLink, isNotNull);

    // Clicks the text link that opens next.html (page navigation)
    expect(
        await _repeatActionWaitingForAbsent(
            browser, nextLink.click, indexTabFinder),
        isTrue,
        reason: 'Failed to click the Next link.');
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(nextTabFinder), isNotNull);
    print('Clicked the next.html link');

    final prevLink = await _waitForWebElement(webdriver, By.linkText('Prev'));
    expect(prevLink, isNotNull);

    // Clicks the text link that opens index.html (page navigation)
    expect(
        await _repeatActionWaitingForAbsent(
            browser, prevLink.click, nextTabFinder),
        isTrue,
        reason: 'Failed to click the Prev link.');

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    print('Clicked the index.html link');

    // Goes back to next.html by tapping the BCK button (history navigation)
    final back = find.byValueKey('back');
    await browser.tap(back);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitForAbsent(indexTabFinder, timeout: _timeoutTenSec);
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(nextTabFinder), isNotNull);
    print('Hit BCK');

    // Goes forward to index.html by tapping the FWD button (history navigation)
    final forward = find.byValueKey('forward');
    await browser.tap(forward);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitForAbsent(nextTabFinder, timeout: _timeoutTenSec);
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    print('Hit FWD');

    // Clicks + button to increase the number
    var digitLink = await _waitForWebElement(webdriver, By.id('target'));
    final addButton = await _waitForWebElement(webdriver, By.id('increase'));
    expect(digitLink.text, '0');
    addButton.click();
    await ermine.waitFor(() async {
      return digitLink.text == '1';
    });
    addButton.click();
    await ermine.waitFor(() async {
      return digitLink.text == '2';
    });
    print('Clicked the + button next to the digit three times');

    // Refreshes the page
    final refresh = find.byValueKey('refresh');
    await browser.tap(refresh);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    digitLink = await _waitForWebElement(webdriver, By.id('target'));
    await ermine.waitFor(() async {
      return digitLink.text == '0';
    });
    print('Hit RFRSH');

    final popupLink = await _waitForWebElement(webdriver, By.linkText('Popup'));
    expect(popupLink, isNotNull);

    // Clicks the text link that opens popup.html (popup page navigation)
    expect(
        await _repeatActionWaitingFor(browser, popupLink.click, popupTabFinder),
        isTrue,
        reason: 'Failed to click the Popup link.');
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    expect(await browser.getText(popupTabFinder), isNotNull);
    print('Clicked the popup.html link');

    await browser.close();
    await ermine.driver.requestData('close');
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    print('Closed the browser');
  });

  test('Should be able to play videos on web pages.', () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

    // Access to video.html where the following video is played:
    // experiences/bin/ermine_testserver/public/simple_browser_test/sample_video.mp4
    // It shows the violet-colored background for the first 3 seconds then shows
    // the fuchsia-colored background for another 3 seconds.
    await input.text('http://127.0.0.1:8080/video.html');
    await input.keyPress(kEnterKey);
    await browser.waitFor(videoTabFinder, timeout: _timeoutTenSec);

    expect(await browser.getText(videoTabFinder), isNotNull);
    print('Opened http://127.0.0.1:8080/video.html');

    // Waits for a while for the video to be loaded before taking a screenshot.
    await Future.delayed(Duration(seconds: 2));
    final earlyScreenshot = await ermine.screenshot(_sampleViewRect);

    // Takes another screenshot after 3 seconds.
    await Future.delayed(Duration(seconds: 3));
    final lateScreenshot = await ermine.screenshot(_sampleViewRect);

    final diff = ermine.screenshotsDiff(earlyScreenshot, lateScreenshot);
    expect(diff, 1, reason: 'The screenshots are more similar than expected.');
    print('The video was played');

    await browser.close();
    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    print('Closed the browser');
  }, skip: true);

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
    await input.text(redUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitFor(redTabFinder, timeout: _timeoutTenSec);
    print('Opened red.html');

    // Opens green.html in the third tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(newTabHintText), timeout: _timeoutTenSec);

    await input.text(greenUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitFor(greenTabFinder, timeout: _timeoutTenSec);
    print('Opened green.html');

    // Opens blue.html in the forth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(newTabHintText), timeout: _timeoutTenSec);

    await input.text(blueUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitFor(blueTabFinder, timeout: _timeoutTenSec);
    print('Opened blue.html');

    // Should have 4 tabs and the forth tab should be focused.
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(redTabFinder), isNotNull);
    expect(await browser.getText(greenTabFinder), isNotNull);
    expect(await browser.getText(blueTabFinder), isNotNull);
    expect(await browser.getText(find.text(blueUrl)), isNotNull);
    print('The Blue tab is focused');

    // The second tab should be focused when tapped.
    await browser.tap(redTabFinder);
    await browser.waitFor(find.text(redUrl));
    expect(await browser.getText(find.text(redUrl)), isNotNull);
    print('Clicked the Red tab');

    // The thrid tab should be focused when tapped.
    await browser.tap(greenTabFinder);
    await browser.waitFor(find.text(greenUrl));
    expect(await browser.getText(find.text(greenUrl)), isNotNull);
    print('Clicked the Green tab');

    /// Tab Rearranging Test

    // Checks the current order of tabs before rearranging tabs.
    expect(await _waitForTabArrangement(browser, newTabFinder, redTabFinder),
        isTrue,
        reason: 'The New tab is not on the left side of the Red tab:');
    expect(await _waitForTabArrangement(browser, redTabFinder, greenTabFinder),
        isTrue,
        reason: 'The Red tab is not on the left side of the Green tab');
    expect(await _waitForTabArrangement(browser, greenTabFinder, blueTabFinder),
        isTrue,
        reason: 'The Green tab is not on the left side of the Blue tab');
    print('The tabs are in the order of New > Red > Green > Blue');

    // Drags the second tab to the right end of the tab list.
    await browser.scroll(redTabFinder, 600, 0, Duration(seconds: 1));

    // The order of tabs after rearranging tabs.
    expect(await _waitForTabArrangement(browser, newTabFinder, greenTabFinder),
        isTrue,
        reason: 'The New tab is not on the left side of the Green tab.');
    expect(await _waitForTabArrangement(browser, greenTabFinder, blueTabFinder),
        isTrue,
        reason: 'The Green tab is not on the left side of the Blue tab');
    expect(await _waitForTabArrangement(browser, blueTabFinder, redTabFinder),
        isTrue,
        reason: 'The Blue tab is not on the left side of the Red tab');
    print('Moved the Red tab to the right end');

    /// Tab closing test
    final tabCloseFinder = find.byValueKey('tab_close');
    await browser.tap(tabCloseFinder);

    // The red page should be gone and the last tab should be focused.
    await browser.waitForAbsent(redTabFinder);
    print('Closed the Red tab');

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(greenTabFinder), isNotNull);
    expect(await browser.getText(blueTabFinder), isNotNull);
    expect(await browser.getText(find.text(blueUrl)), isNotNull);
    print('The Blue tab is focused');

    // TODO(fxb/70265): Test closing an unfocused tab once fxb/68689 is done.

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    await browser.close();
    print('Closed the browser');
  }, skip: true);

  test('Should be able enter text into web text fields', () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

    const testInputPage = 'http://127.0.0.1:8080/input.html';
    final textInputTabFinder = find.text('Text Input');

    // Access to the website.
    await input.text(testInputPage);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitFor(textInputTabFinder, timeout: _timeoutTenSec);
    print('Opened $testInputPage');

    final webdriver =
        (await webDriverConnector.webDriversForHost('127.0.0.1')).single;

    final textField = await _waitForWebElement(webdriver, By.id('text-input'));
    print('The textfield is found.');

    expect(textField, isNotNull);

    textField.click();
    await ermine.waitFor(() async {
      return webdriver.activeElement.equals(textField);
    }, timeout: _timeoutTenSec);
    print('The textfield is now focused.');

    // TODO(fxb/74070): Sl4f.Input currently does not work for web elements.
    // Replace the following line with Sl4f.Input once that is fixed.
    const testText = 'hello fuchsia';
    textField.sendKeys(testText);
    await ermine.waitFor(() async {
      return textField.properties['value'] == testText;
    }, timeout: _timeoutTenSec);
    print('Text is entered into the textfield.');

    await browser.close();
    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    print('Closed the browser');
  }, skip: true);
  // TODO(fxb/68716): Test audio playing
}
