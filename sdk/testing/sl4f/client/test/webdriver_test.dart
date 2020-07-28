// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:webdriver/sync_core.dart';

import 'package:sl4f/sl4f.dart';

class MockSsh extends Mock implements Ssh {}

class MockSl4f extends Mock implements Sl4f {}

class MockProcessHelper extends Mock implements ProcessHelper {}

class MockWebDriverHelper extends Mock implements WebDriverHelper {}

class MockWebDriver extends Mock implements WebDriver {}

void main(List<String> args) {
  MockSl4f sl4f;
  MockProcessHelper processHelper;
  MockWebDriverHelper webDriverHelper;
  WebDriverConnector webDriverConnector;

  setUp(() {
    sl4f = MockSl4f();
    processHelper = MockProcessHelper();
    webDriverHelper = MockWebDriverHelper();
    webDriverConnector = WebDriverConnector('path/to/chromedriver', sl4f,
        processHelper: processHelper, webDriverHelper: webDriverHelper);
  });

  test('webDriversForHost filters by host', () async {
    final openContexts = {
      20000: 'https://www.test.com/path/1',
      20001: 'https://www.example.com/path/1',
      20002: 'https://www.test.com/path/2',
      20003: 'https://www.example.com/path/2'
    };
    mockAvailableWebDrivers(webDriverHelper, sl4f, openContexts);
    final webDrivers =
        await webDriverConnector.webDriversForHost('www.test.com');
    expect(webDrivers.length, 2);
    final webDriverCurrentUrls =
        Set.from(webDrivers.map((webDriver) => webDriver.currentUrl));
    expect(webDriverCurrentUrls,
        {'https://www.test.com/path/1', 'https://www.test.com/path/2'});
  });

  test('webDriversForHost refresh session', () async {
    final openContexts = {
      20000: 'https://www.test.com/path/1',
      20001: 'https://www.example.com/path/1',
    };
    mockAvailableWebDrivers(webDriverHelper, sl4f, openContexts);

    final webDrivers =
        await webDriverConnector.webDriversForHost('www.test.com');
    expect(webDrivers.length, 1);

    // Keep port 20000 active
    when(sl4f.request('webdriver_facade.GetDevToolsPorts'))
        .thenAnswer((_) => Future.value({
              'ports': [20000]
            }));

    // Expire session by throwing NoSuchWindowException.
    when(webDrivers.single.window).thenAnswer(
        (_) => throw NoSuchWindowException(1, 'Session not displayed'));

    when(sl4f.request('proxy_facade.OpenProxy', any)).thenAnswer((invocation) {
      final targetPort = invocation.positionalArguments[1];
      return Future.value(targetPort + 10);
    });

    when(webDriverHelper.createDriver(any, any, any, any))
        .thenAnswer((invocation) {
      WebDriver webDriver = MockWebDriver();
      when(webDriver.currentUrl).thenReturn('https://www.test.com/path/2');
      return webDriver;
    });

    final result = await webDriverConnector.webDriversForHost('www.test.com');
    expect(result.single.currentUrl, 'https://www.test.com/path/2');
  });

  test('webDriversForHost no contexts', () async {
    mockAvailableWebDrivers(webDriverHelper, sl4f, {});
    var webDrivers = await webDriverConnector.webDriversForHost('www.test.com');
    expect(webDrivers.length, 0);
  });
}

/// Set up mocks as if there are chrome contexts with the given ports exposing a url.
void mockAvailableWebDrivers(MockWebDriverHelper webDriverHelper, MockSl4f sl4f,
    Map<int, String> targetPortToUrl) {
  final targetPortList = {'ports': List.from(targetPortToUrl.keys)};
  when(sl4f.request('webdriver_facade.GetDevToolsPorts'))
      .thenAnswer((_) => Future.value(targetPortList));

  // Pretend that open port == target port + 10, this lets us easily convert
  // between the two for mocking.
  when(sl4f.request('proxy_facade.OpenProxy', any)).thenAnswer((invocation) {
    final targetPort = invocation.positionalArguments[1];
    return Future.value(targetPort + 10);
  });

  when(webDriverHelper.createDriver(any, any, any, any))
      .thenAnswer((invocation) {
    final openPort = invocation.positionalArguments[1];
    final targetPort = openPort - 10;
    WebDriver webDriver = MockWebDriver();
    when(webDriver.currentUrl).thenReturn(targetPortToUrl[targetPort]);
    return webDriver;
  });
}
