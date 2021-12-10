// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';
import 'package:webdriver/async_core.dart';

class MockSl4f extends Mock implements Sl4f {}

class MockPortForwarder extends Mock implements PortForwarder {}

class MockWebDriverHelper extends Mock implements WebDriverHelper {}

class MockWebDriver extends Mock implements WebDriver {
  final String url;
  final Exception exception;
  MockWebDriver(this.url, this.exception);

  @override
  Future<Window> get window {
    if (exception != null) {
      throw exception;
    }
    // This does not match the real class, but we don't use the return value.
    return null;
  }

  @override
  Future<String> get currentUrl => Future.value(url);
}

// These magic numbers don't affect the tests.
final devtoolsAccessPoint = HostAndPort('127.0.0.1', 1234);
final chromeDriverUri = Uri.parse('http://127.0.0.1:8000');

void main(List<String> args) {
  MockSl4f sl4f;
  MockPortForwarder portForwarder;
  MockWebDriverHelper webDriverHelper;
  SingleWebDriverConnector webDriverConnector;

  setUp(() {
    sl4f = MockSl4f();
    portForwarder = MockPortForwarder();
    webDriverHelper = MockWebDriverHelper();
    webDriverConnector = SingleWebDriverConnector(chromeDriverUri, sl4f,
        webDriverHelper: webDriverHelper, portForwarder: portForwarder);

    // Note that 9222 is always added to the port list.
    when(sl4f.request('webdriver_facade.GetDevToolsPorts'))
        .thenAnswer((_) => Future.value({'ports': []}));
    when(portForwarder.forwardPort(any))
        .thenAnswer((_) => Future.value(devtoolsAccessPoint));
  });

  test('webDriverForHosts reuses current webdriver', () async {
    setUpMockWebDriverHelper(webDriverHelper, [
      MockWebDriver('http://example.com', null),
    ]);
    var webDriver =
        await webDriverConnector.webDriverForHosts(['test.com', 'example.com']);
    expect(await webDriver.currentUrl, 'http://example.com');
    verify(webDriverHelper.createAsyncDriver(any, any)).called(1);
    verify(portForwarder.forwardPort(any)).called(1);

    webDriver = await webDriverConnector.webDriverForHosts(['example.com']);
    expect(await webDriver.currentUrl, 'http://example.com');
    verifyNever(webDriverHelper.createAsyncDriver(any, any));
    verifyNever(portForwarder.forwardPort(any));
  });

  test('webDriverForHosts iterates to find the right context', () async {
    when(sl4f.request('webdriver_facade.GetDevToolsPorts'))
        .thenAnswer((_) => Future.value({
              'ports': [9222, 9223, 9224]
            }));
    setUpMockWebDriverHelper(webDriverHelper, [
      MockWebDriver('http://example.com', NoSuchWindowException(null, null)),
      MockWebDriver('http://test.com', null),
      MockWebDriver('http://example.com', null),
    ]);

    final webDriver =
        await webDriverConnector.webDriverForHosts(['example.com']);
    expect(await webDriver.currentUrl, 'http://example.com');
    verify(webDriverHelper.createAsyncDriver(any, any)).called(3);
    verify(portForwarder.forwardPort(9222)).called(1);
    verify(portForwarder.forwardPort(9223)).called(1);
    verify(portForwarder.forwardPort(9224)).called(1);
    verify(portForwarder.stopPortForwarding(any, any)).called(2);
  });
}

void setUpMockWebDriverHelper(
    MockWebDriverHelper webDriverHelper, List<MockWebDriver> webDrivers) {
  when(webDriverHelper.createAsyncDriver(devtoolsAccessPoint, chromeDriverUri))
      .thenAnswer((_) => Future.value(webDrivers.removeAt(0)));
}
