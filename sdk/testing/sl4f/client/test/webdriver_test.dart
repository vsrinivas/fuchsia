// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:webdriver/sync_core.dart';

import 'package:sl4f/sl4f.dart';

class MockPortForwarder extends Mock implements PortForwarder {}

class MockSl4f extends Mock implements Sl4f {}

class MockProcessHelper extends Mock implements ProcessHelper {}

class MockWebDriverHelper extends Mock implements WebDriverHelper {}

class MockWebDriver extends Mock implements WebDriver {}

const String testDutAddress = '192.168.1.1';

void main(List<String> args) {
  MockSl4f sl4f;
  MockPortForwarder portForwarder;
  MockProcessHelper processHelper;
  MockWebDriverHelper webDriverHelper;
  WebDriverConnector webDriverConnector;

  setUp(() {
    sl4f = MockSl4f();
    portForwarder = MockPortForwarder();
    processHelper = MockProcessHelper();
    webDriverHelper = MockWebDriverHelper();
    webDriverConnector = WebDriverConnector('path/to/chromedriver', sl4f,
        processHelper: processHelper,
        webDriverHelper: webDriverHelper,
        portForwarder: portForwarder);
  });

  test('webDriversForHost filters by host', () async {
    final openContexts = {
      20000: 'https://www.test.com/path/1',
      20001: 'https://www.example.com/path/1',
      20002: 'https://www.test.com/path/2',
      20003: 'https://www.example.com/path/2'
    };
    mockAvailableWebDrivers(webDriverHelper, sl4f, portForwarder, openContexts);
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
    mockAvailableWebDrivers(webDriverHelper, sl4f, portForwarder, openContexts);

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

    when(portForwarder.forwardPort(any)).thenAnswer((invocation) {
      final remotePort = invocation.positionalArguments.first;
      return Future.value(HostAndPort(testDutAddress, remotePort + 10));
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
    mockAvailableWebDrivers(webDriverHelper, sl4f, portForwarder, {});
    var webDrivers = await webDriverConnector.webDriversForHost('www.test.com');
    expect(webDrivers.length, 0);
  });

  test('port forwarder chosen correctly', () {
    // most targets should use the tcp proxy.
    final tcpTargets = ['[::1]', '[fe80::1]', '192.168.0.10', '127.0.0.1'];
    for (String tcpTarget in tcpTargets) {
      final mockSl4f = MockSl4f();
      when(mockSl4f.target).thenReturn(tcpTarget);
      final webdriverConnector =
          WebDriverConnector('/path/chromedriver', mockSl4f);
      expect(webdriverConnector.portForwarder, isA<TcpPortForwarder>());
    }
    // since chromedriver cannot handle ipv6 zone id, we fallback to ssh port forwarding.
    final sshTargets = ['[fe80::1%zone]'];
    for (String sshTarget in sshTargets) {
      final mockSl4f = MockSl4f();
      when(mockSl4f.target).thenReturn(sshTarget);
      final webdriverConnector =
          WebDriverConnector('/path/chromedriver', mockSl4f);
      expect(webdriverConnector.portForwarder, isA<SshPortForwarder>());
    }
  });
}

/// Set up mocks as if there are chrome contexts with the given ports exposing a url.
void mockAvailableWebDrivers(MockWebDriverHelper webDriverHelper, MockSl4f sl4f,
    MockPortForwarder portForwarder, Map<int, String> remotePortToUrl) {
  final remotePortList = {'ports': List.from(remotePortToUrl.keys)};
  when(sl4f.request('webdriver_facade.GetDevToolsPorts'))
      .thenAnswer((_) => Future.value(remotePortList));

  // Pretend that open port == remote port + 10, this lets us easily convert
  // between the two for mocking.
  when(portForwarder.forwardPort(any)).thenAnswer((invocation) {
    final remotePort = invocation.positionalArguments.first;
    return Future.value(HostAndPort(testDutAddress, remotePort + 10));
  });

  when(webDriverHelper.createDriver(any, any)).thenAnswer((invocation) {
    final accessPoint = invocation.positionalArguments.first;
    expect(accessPoint.host, equals(testDutAddress));
    final openPort = accessPoint.port;
    final remotePort = openPort - 10;
    WebDriver webDriver = MockWebDriver();
    when(webDriver.currentUrl).thenReturn(remotePortToUrl[remotePort]);
    return webDriver;
  });
}
