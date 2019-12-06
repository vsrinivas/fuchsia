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
  MockSsh ssh;
  MockProcessHelper processHelper;
  MockWebDriverHelper webDriverHelper;
  WebDriverConnector webDriverConnector;

  setUp(() {
    sl4f = MockSl4f();
    ssh = MockSsh();
    when(sl4f.ssh).thenReturn(ssh);
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
    mockAvailableWebDrivers(webDriverHelper, sl4f, ssh, openContexts);
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
    mockAvailableWebDrivers(webDriverHelper, sl4f, ssh, openContexts);

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

    when(ssh.forwardPort(remotePort: anyNamed('remotePort')))
        .thenAnswer((invocation) {
      final remotePort = invocation.namedArguments[#remotePort];
      return Future.value(remotePort + 10);
    });

    when(webDriverHelper.createDriver(any, any)).thenAnswer((invocation) {
      WebDriver webDriver = MockWebDriver();
      when(webDriver.currentUrl).thenReturn('https://www.test.com/path/2');
      return webDriver;
    });

    final result = await webDriverConnector.webDriversForHost('www.test.com');
    expect(result.single.currentUrl, 'https://www.test.com/path/2');
  });

  test('webDriversForHost no contexts', () async {
    mockAvailableWebDrivers(webDriverHelper, sl4f, ssh, {});
    var webDrivers = await webDriverConnector.webDriversForHost('www.test.com');
    expect(webDrivers.length, 0);
  });
}

/// Set up mocks as if there are chrome contexts with the given ports exposing a url.
void mockAvailableWebDrivers(MockWebDriverHelper webDriverHelper, MockSl4f sl4f,
    MockSsh ssh, Map<int, String> remotePortToUrl) {
  final remotePortList = {'ports': List.from(remotePortToUrl.keys)};
  when(sl4f.request('webdriver_facade.GetDevToolsPorts'))
      .thenAnswer((_) => Future.value(remotePortList));

  // Pretend that local port == remote port + 10, this lets us easily convert
  // between the two for mocking.
  when(ssh.forwardPort(remotePort: anyNamed('remotePort')))
      .thenAnswer((invocation) {
    final remotePort = invocation.namedArguments[#remotePort];
    return Future.value(remotePort + 10);
  });

  when(webDriverHelper.createDriver(any, any)).thenAnswer((invocation) {
    final localPort = invocation.positionalArguments.first;
    final remotePort = localPort - 10;
    WebDriver webDriver = MockWebDriver();
    when(webDriver.currentUrl).thenReturn(remotePortToUrl[remotePort]);
    return webDriver;
  });
}
