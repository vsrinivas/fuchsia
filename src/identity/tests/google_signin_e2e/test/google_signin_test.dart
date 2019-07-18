// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This file contains a host-side test driver that runs Google login tests
/// on a remote device under test.

import 'dart:io' as io;
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' show WebDriverConnector, Sl4f, Inspect;
import 'package:webdriver/sync_io.dart'
    show By, WebDriver, WebElement, WebDriverException;
import 'package:webdriver/support/async.dart';

/// URL for the tester mod.
const _testerUrl =
    'fuchsia-pkg://fuchsia.com/google_signin_e2e_tester#meta/google_signin_e2e_tester.cmx';
const _testerComponent = 'google_signin_e2e_tester.cmx';

/// Flags to pass to the auth tester mod.
const _testerFlags = [];

/// Chromedriver location.
const _chromedriverPath = 'runtime_deps/chromedriver';

/// Number of polling attempts to make when finding Chrome contexts
const chromePollCount = 5;

/// Time between retries when polling
const pollDelay = Duration(seconds: 1);

void main() {
  Sl4f sl4fDriver;
  Inspect inspect;
  WebDriverConnector webdriverConnector;
  WebDriver webdriver;

  setUp(() async {
    sl4fDriver = Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    inspect = Inspect(sl4fDriver.ssh);
    webdriverConnector = WebDriverConnector(_chromedriverPath, sl4fDriver);
    await webdriverConnector.initialize();
    await startLoginTester(sl4fDriver);
    webdriver = await pollForWebdriver(webdriverConnector);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
    webdriverConnector.tearDown();
    await sl4fDriver.ssh.run('tiles_ctl quit');
  });

  test('Authenticate through Google using Chromium', () async {
    await _driveGoogleLogin(webdriver);
    await assertSuccessful(inspect);
  }, timeout: Timeout(Duration(minutes: 2)));
}

/// Polls for a Chrome context until it is ready.
Future<WebDriver> pollForWebdriver(WebDriverConnector connector,
    {int pollAttempts = chromePollCount,
    Duration attemptDelay = pollDelay}) async {
  for (var attempt = 0; attempt < pollAttempts; attempt++) {
    var ports = await connector.webDriversForHost('accounts.google.com');
    if (ports.length == 1) {
      return ports.first;
    }
    await Future.delayed(attemptDelay);
  }
  throw AuthTestException('Timed out searching for Chrome instance');
}

/// Drives simple happy case login for glif UI.
Future<void> _driveGoogleLogin(WebDriver webdriver) async {
  // TODO(AUTH-201): obtain credentials programmatically from RHEA.
  var email = io.Platform.environment['FUCHSIA_TEST_ACCOUNT_EMAIL'];
  var password = io.Platform.environment['FUCHSIA_TEST_ACCOUNT_PASSWORD'];

  // Identifier page.
  var emailEntry = await getElementWhenReady(
      webdriver, By.cssSelector('input[name="identifier"]'));
  emailEntry.sendKeys(email);
  webdriver.findElement(By.id('identifierNext')).click();

  // Password challenge page.
  var passEntry = await getElementWhenReady(
      webdriver, By.cssSelector('input[name="password"]'));
  passEntry.sendKeys(password);
  webdriver.findElement(By.id('passwordNext')).click();

  // OAuth consent page.  May not appear if login previously done with this account,
  // in which case authentication just succeeds without driving the page.
  try {
    var acceptButton =
        await getElementWhenReady(webdriver, By.id('submit_approve_access'));
    acceptButton.click();
  } on WebDriverException {
    // This most likely indicates Chrome terminated due to completing login without
    // the consent page.  Just ignore the error and continue on to verify if login
    // succeeded.
  }

  // TODO(satsukiu): Identify and drive common interstitial pages as they come up.
}

/// Starts the login tester mod on the DuT.
Future<void> startLoginTester(Sl4f sl4f) async {
  await sl4f.ssh.run('tiles_ctl start');
  var runResult =
      await sl4f.ssh.run('tiles_ctl add $_testerUrl ${_testerFlags.join(" ")}');
  // tiles_ctl add can fail if tiles is not ready yet.
  while (runResult.exitCode != 0) {
    await Future.delayed(pollDelay);
    runResult = await sl4f.ssh
        .run('tiles_ctl add $_testerUrl ${_testerFlags.join(" ")}');
  }
}

/// Blocks until the referenced element is interactable, then returns it.  This
/// method raises an `WebDriverException` if the element is not found in time.
Future<WebElement> getElementWhenReady(WebDriver webdriver, By by) async {
  var element = await Clock().waitFor(() {
    return webdriver.findElement(by);
  });
  await Clock().waitFor(() {
    return element.displayed;
  }, matcher: (bool displayed) {
    return displayed;
  });
  await Clock().waitFor(() {
    return element.enabled;
  }, matcher: (bool enabled) {
    return enabled;
  });
  return element;
}

/// Asserts success by checking the inspect state of the tester mod.
Future<void> assertSuccessful(Inspect inspect) async {
  for (;;) {
    var inspectJson = await inspect.inspectComponentRoot(_testerComponent);
    var authStatus = inspectJson['auth-status'];
    if (authStatus == 'success') {
      return;
    } else if (authStatus == 'failure') {
      fail('Authentication failed!');
    }
  }
}

class AuthTestException implements Exception {
  String error;
  AuthTestException(this.error);

  @override
  String toString() {
    return 'Error during test: $error';
  }
}
