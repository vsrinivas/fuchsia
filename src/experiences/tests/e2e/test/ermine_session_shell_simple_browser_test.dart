// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: import_of_legacy_library_into_null_safe

import 'dart:async';
import 'dart:math';

import 'package:ermine_driver/ermine_driver.dart';
import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:fidl_fuchsia_ui_input3/fidl_async.dart' hide KeyEvent;
import 'package:flutter_driver/flutter_driver.dart';
import 'package:gcloud_lib/gcloud_lib.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';
import 'package:webdriver/sync_io.dart';

const _timeoutOneSec = Duration(seconds: 1);
const _timeoutThreeSec = Duration(seconds: 3);
const _timeoutTenSec = Duration(seconds: 10);
const _sampleViewRect = Rectangle(100, 200, 100, 100);
const testserverUrl =
    'fuchsia-pkg://fuchsia.com/ermine_testserver#meta/ermine_testserver.cmx';

// Flags to enable/disable each test in order of
// 0: Web pages & history navigation test
// 1: Video test
// 2: Tab control test
// 3: Text input field test
// 4: Audio test
// 5: Keyboard shortcut test.
const skipTests = [false, true, true, true, true, true];

void main() {
  late Sl4f sl4f;
  late ErmineDriver ermine;
  late WebDriverConnector webDriverConnector;
  late Input input;

  // TODO(fxb/69334): Get rid of the space in the hint text.
  const newTabHintText = '     SEARCH';
  const indexUrl = 'http://127.0.0.1:8080/index.html';
  final newTabFinder = find.text('NEW TAB');
  final indexTabFinder = find.text('Localhost');
  final nextTabFinder = find.text('Next Page');
  final popupTabFinder = find.text('Popup Page');
  final videoTabFinder = find.text('Video Test');
  final redTabFinder = find.text('Red Page');
  final greenTabFinder = find.text('Green Page');
  final blueTabFinder = find.text('Blue Page');
  final audioTabFinder = find.text('Audio Test');

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();

    input = Input(sl4f);

    webDriverConnector = WebDriverConnector('runtime_deps/chromedriver', sl4f);
    await webDriverConnector.initialize();

    // Starts hosting a local http website if there's any test running.
    if (skipTests.any((isSkipped) => !isSkipped)) {
      // ignore: unawaited_futures
      ermine.component.launch(testserverUrl);
      expect(await ermine.isRunning(testserverUrl), isTrue);
    }
  });

  tearDownAll(() async {
    // Closes the test server if there's any test running.
    // simple-browser is launched via [Component.launch()] since it does not
    // have a view. Therefore, it cannot be closed with ermine's flutter driver.
    // For this reason, we have to explicitly stop the http server to avoid
    // HttpException which occurs in case the test is torn down still having it
    // running.
    // TODO(fxb/69291): Remove this workaround once we can properly close hidden
    // components
    if (await ermine.isRunning(testserverUrl, timeout: _timeoutTenSec)) {
      FlutterDriver browser =
          await ermine.launchAndWaitForSimpleBrowser(openNewTab: false);
      const stopUrl = 'http://127.0.0.1:8080/stop';
      await browser.requestData(stopUrl);
      await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
      await browser.waitFor(find.text(stopUrl), timeout: _timeoutTenSec);
      print('Waiting for the test server to stop...');
      expect(await ermine.isStopped(testserverUrl), isTrue);
      print('Stopped the test server');

      await browser.close();
      await ermine.driver.requestData('close');
      await ermine.driver
          .waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
      await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
      expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
      print('Closed the browser');
    }

    await webDriverConnector.tearDown();
    await ermine.tearDown();
    await sl4f.stopServer();
    sl4f.close();
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
  Future<WebElement?> _waitForWebElement(WebDriver web, By by) async {
    return await ermine.waitFor<WebElement?>(() async {
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
    FlutterDriver browser,
    void action(),
    SerializableFinder finder, {
    Duration waitForTimeout = _timeoutOneSec,
  }) async {
    return await _repeatActionUntilGetResult(
        action, () => browser.waitFor(finder, timeout: waitForTimeout));
  }

  /// Keeps calling `waitForAbsent(finder)` until the timeout expires. Returns
  /// true if it locates one. Otherwise, returns false.
  Future<bool> _repeatActionWaitingForAbsent(
    FlutterDriver browser,
    void action(),
    SerializableFinder finder, {
    Duration waitForAbsentTimeout = _timeoutOneSec,
  }) async {
    return await _repeatActionUntilGetResult(action,
        () => browser.waitForAbsent(finder, timeout: waitForAbsentTimeout));
  }

  Future<void> _invokeShortcut(List<Key> keys) async {
    const pressDuration = 100;
    final releaseDuration = pressDuration * keys.length + 100;
    var pressDurations = [
      for (var i = 0; i < keys.length; i++) pressDuration + (i * 100)
    ];
    var releaseDurations = [
      for (var i = 0; i < keys.length; i++) releaseDuration + (i * 100)
    ];

    await input.keyEvents([
      for (var i = 0; i < keys.length; i++)
        KeyEvent(keys[i], Duration(milliseconds: pressDurations[i]),
            KeyEventType.pressed),
      // Releases the key in reverse order.
      for (var i = 0; i < keys.length; i++)
        KeyEvent(keys[keys.length - i - 1],
            Duration(milliseconds: releaseDurations[i]), KeyEventType.released)
    ]);

    await ermine.driver
        .waitUntilNoTransientCallbacks(timeout: Duration(seconds: 2));
  }

  // TODO(fxb/68689): Transition pointer interactions to Sl4f.Input once it is
  // ready.
  test('Should be able to do page and history navigation.', () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

    // Access to the website.
    await input.text(indexUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitFor(indexTabFinder, timeout: _timeoutTenSec);

    final webdriver =
        (await webDriverConnector.webDriversForHost('127.0.0.1')).single;

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    print('Opened $indexUrl');

    final nextLink = await _waitForWebElement(webdriver, By.linkText('Next'));
    expect(nextLink, isNotNull);

    // Clicks the text link that opens next.html (page navigation)
    expect(
        await _repeatActionWaitingForAbsent(
            browser, nextLink!.click, indexTabFinder),
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
            browser, prevLink!.click, nextTabFinder),
        isTrue,
        reason: 'Failed to click the Prev link.');

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    print('Clicked the index.html link');

    // Goes back to next.html by tapping the BCK button (history navigation)
    expect(
      await _repeatActionWaitingForAbsent(browser, () async {
        final back = find.byValueKey('back');
        await browser.tap(back);
      }, indexTabFinder),
      isTrue,
      reason: 'Failed to hit the BCK button.',
    );

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(nextTabFinder), isNotNull);
    print('Hit BCK');

    // Goes forward to index.html by tapping the FWD button (history navigation)
    expect(
      await _repeatActionWaitingForAbsent(browser, () async {
        final forward = find.byValueKey('forward');
        await browser.tap(forward);
      }, nextTabFinder),
      isTrue,
      reason: 'Failed to hit the FWD button.',
    );

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    print('Hit FWD');

    // Clicks + button to increase the number
    var digitLink = await _waitForWebElement(webdriver, By.id('target'));
    final addButton = await _waitForWebElement(webdriver, By.id('increase'));
    expect(digitLink!.text, '0');
    addButton!.click();
    await ermine.waitFor(() async {
      return digitLink!.text == '1';
    });
    addButton.click();
    await ermine.waitFor(() async {
      return digitLink!.text == '2';
    });
    print('Clicked the + button next to the digit three times');

    // Refreshes the page
    final refresh = find.byValueKey('refresh');
    await browser.tap(refresh);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    digitLink = await _waitForWebElement(webdriver, By.id('target'));
    await ermine.waitFor(() async {
      return digitLink!.text == '0';
    });
    print('Hit RFRSH');

    final popupLink = await _waitForWebElement(webdriver, By.linkText('Popup'));
    expect(popupLink, isNotNull);

    // Clicks the text link that opens popup.html (popup page navigation)
    expect(
        await _repeatActionWaitingFor(browser, popupLink!.click, popupTabFinder,
            waitForTimeout: _timeoutThreeSec),
        isTrue,
        reason: 'Failed to click the Popup link.');
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    expect(await browser.getText(popupTabFinder), isNotNull);
    print('Clicked the popup.html link');

    await browser.close();
    // Close the simple browser view.
    await ermine.threeKeyShortcut(Key.leftCtrl, Key.leftShift, Key.w);
    await ermine.driver.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    print('Closed the browser');
  }, skip: skipTests[0]);

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

    final isVideoPlayed = await ermine.waitFor(() async {
      final lateScreenshot = await ermine.screenshot(_sampleViewRect);
      final diff = ermine.screenshotsDiff(earlyScreenshot, lateScreenshot);
      return diff == 1;
    }, timeout: _timeoutTenSec);

    expect(isVideoPlayed, isTrue);
    print('The video was played');

    await browser.close();
    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    print('Closed the browser');
  }, skip: skipTests[1]);

  test('Should be able to switch, rearrange, and close tabs', () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

    /// Tab Switching Test
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
  }, skip: skipTests[2]);

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

    textField!.click();
    await ermine.waitFor(() async {
      return webdriver.activeElement!.equals(textField);
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
  }, skip: skipTests[3]);

  test('Should be able play audios on web', () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

    final record = Audio(sl4f);
    final gcloud = GCloud();

    // Access to audio.html where the following audio is played:
    // experiences/bin/ermine_testserver/public/simple_browser_test/sample_audio.mp3
    // It plays human voice saying "How old is Obama".
    await input.text('http://127.0.0.1:8080/audio.html');
    await input.keyPress(kEnterKey);
    await browser.waitFor(audioTabFinder, timeout: _timeoutTenSec);

    expect(await browser.getText(audioTabFinder), isNotNull);
    print('Opened http://127.0.0.1:8080/audio.html');

    final webdriver =
        (await webDriverConnector.webDriversForHost('127.0.0.1')).single;

    final audio = await _waitForWebElement(webdriver, By.id('audio'));
    expect(audio, isNotNull);
    print('The textfield is found.');

    final playButton = await _waitForWebElement(webdriver, By.id('play'));
    expect(playButton, isNotNull);
    print('The PLAY button is found.');

    // Note that it doesn't work locally. You should create GCloud using
    // `GCloud.withClientViaApiKey()` with an API key for local testing.
    await gcloud.setClientFromMetadata();
    print('Set an authenticated gcloud client');

    // Plays the audio, records it, sends it to gcloud for speech-to-text, and
    // verifies if the text result is what we expect.
    // Retries this process for a few more times if it fails since the audio
    // sometimes sounds janky.
    final ttsResult = await ermine.waitFor(() async {
      print('Start recording audio.');
      await record.startOutputSave();
      await Future.delayed(_timeoutOneSec);
      playButton!.click();

      // Waits for the audio being played to the end.
      await Future.delayed(Duration(seconds: 5));

      await record.stopOutputSave();
      final audioOutput = await record.getOutputAudio();
      print('Stopped recording audio.');

      final ttsList =
          await speechToText(gcloud.speech, audioOutput.audioData, 'en-us');
      final tts = ttsList.first.toLowerCase();
      print('STT result: $tts');
      return tts == 'how old is obama';
    });

    expect(ttsResult, isTrue);

    gcloud.close();
    await browser.close();
    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    print('Closed the browser');
  }, skip: skipTests[4]);

  test('Should be able to control the browser with keyboard shortcuts',
      () async {
    FlutterDriver browser;
    browser = await ermine.launchAndWaitForSimpleBrowser();

    // Opens index.html
    await input.text(indexUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitFor(indexTabFinder, timeout: _timeoutTenSec);

    final webdriver =
        (await webDriverConnector.webDriversForHost('127.0.0.1')).single;
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    print('Opened $indexUrl');

    // Clicks the + buttons
    var digitLink = await _waitForWebElement(webdriver, By.id('target'));
    final addButton = await _waitForWebElement(webdriver, By.id('increase'));
    expect(digitLink!.text, '0');
    addButton!.click();
    await ermine.waitFor(() async {
      return digitLink!.text == '1';
    });
    addButton.click();
    await ermine.waitFor(() async {
      return digitLink!.text == '2';
    });
    print('Clicked the + button next to the digit three times');

    // Shortcut for refresh (Ctrl + r)
    await _invokeShortcut([Key.leftCtrl, Key.r]);
    digitLink = await _waitForWebElement(webdriver, By.id('target'));
    await ermine.waitFor(() async {
      return digitLink!.text == '0';
    });
    print('Refreshed the page');

    // Clicks the 'Next' link
    final nextLink = await _waitForWebElement(webdriver, By.linkText('Next'));
    expect(nextLink, isNotNull);
    expect(
        await _repeatActionWaitingForAbsent(
            browser, nextLink!.click, indexTabFinder),
        isTrue,
        reason: 'Failed to click the Next link.');
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(nextTabFinder), isNotNull);
    print('Clicked the next.html link');

    // Shortcut for backward (Alt + ←)
    expect(
        await _repeatActionWaitingForAbsent(
            browser,
            () async => await _invokeShortcut([Key.leftAlt, Key.left]),
            nextTabFinder),
        isTrue,
        reason: 'Failed to invoke the shortcut for navigating back.');
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    print('Navigated back to index.html');

    // Shortcut for forward (Alt + →)
    expect(
        await _repeatActionWaitingForAbsent(
            browser,
            () async => await _invokeShortcut([Key.leftAlt, Key.right]),
            nextTabFinder),
        isTrue,
        reason: 'Failed to invoke the shortcut for navigating forward.');
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(indexTabFinder), isNotNull);
    print('Navigated forward to next.html');

    // Shortcut for opening a new tab (Ctrl + t)
    await _invokeShortcut([Key.leftCtrl, Key.t]);
    await browser.waitFor(find.text(newTabHintText), timeout: _timeoutTenSec);

    // Opens blue.html
    const blueUrl = 'http://127.0.0.1:8080/blue.html';
    const nextUrl = 'http://127.0.0.1:8080/next.html';
    await input.text(blueUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeoutTenSec);
    await browser.waitFor(blueTabFinder, timeout: _timeoutTenSec);
    print('Opened blue.html');

    // Shortcut for selecting the next tab (Ctrl + Tab) x 2
    await _invokeShortcut([Key.leftCtrl, Key.tab]);
    await browser.waitFor(find.text(newTabHintText));
    expect(await browser.getText(find.text(newTabHintText)), isNotNull);
    print('The new tab is now selected');

    await _invokeShortcut([Key.leftCtrl, Key.tab]);
    await browser.waitFor(find.text(nextUrl));
    expect(await browser.getText(find.text(nextUrl)), isNotNull);
    print('The next tab is now selected');

    // Shortcut for selecting the previous tab (Ctrl + Shift +Tab) x 2
    await _invokeShortcut([Key.leftCtrl, Key.leftShift, Key.tab]);
    await browser.waitFor(find.text(newTabHintText));
    expect(await browser.getText(find.text(newTabHintText)), isNotNull);
    print('The new tab is now selected');

    await _invokeShortcut([Key.leftCtrl, Key.leftShift, Key.tab]);
    await browser.waitFor(find.text(blueUrl));
    expect(await browser.getText(find.text(blueUrl)), isNotNull);
    print('The blue tab is now selected');

    // Shortcut for closing a current tab (Ctrl + w)
    await _invokeShortcut([Key.leftCtrl, Key.w]);
    await browser.waitForAbsent(blueTabFinder);
    print('Closed the blue tab');

    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(find.text(nextUrl)), isNotNull);
    print('The index tab is focused');

    await browser.close();
    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
    print('Closed the browser');
  }, skip: skipTests[5]);
}
