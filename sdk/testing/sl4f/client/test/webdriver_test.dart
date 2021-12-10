// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';
import 'package:webdriver/sync_core.dart';

class MockPortForwarder extends Mock implements PortForwarder {}

class MockSl4f extends Mock implements Sl4f {}

class MockProcessHelper extends Mock implements ProcessHelper {}

class MockWebDriverHelper extends Mock implements WebDriverHelper {}

class MockWebDriver extends Mock implements WebDriver {}

class MockWindow extends Mock implements Window {}

class MockTcpProxyController extends Mock implements TcpProxyController {}

class MockHttpClient extends Mock implements HttpClient {}

class MockHttpClientRequest extends Mock implements HttpClientRequest {
  final bool _success;

  MockHttpClientRequest({bool success})
      : headers = MockHttpHeaders(),
        _success = success ?? true;

  @override
  final HttpHeaders headers;

  @override
  Future<HttpClientResponse> close() async {
    if (!_success) {
      throw MockHttpException();
    }
    return MockHttpClientResponse();
  }
}

class MockHttpClientResponse extends Mock implements HttpClientResponse {}

class MockHttpHeaders extends Mock implements HttpHeaders {}

class MockHttpException implements Exception {}

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

    expect(await webDriverConnector.webDriversForHost('www.test.com'), isEmpty);

    when(portForwarder.forwardPort(any)).thenAnswer((invocation) {
      final remotePort = invocation.positionalArguments.first;
      return Future.value(HostAndPort(testDutAddress, remotePort + 10));
    });

    when(webDriverHelper.createDriver(any, any)).thenAnswer((invocation) async {
      WebDriver webDriver = MockWebDriver();
      when(webDriver.currentUrl).thenReturn('https://www.test.com/path/2');
      return webDriver;
    });

    // Stop expiring sessions
    when(webDrivers.single.window).thenAnswer((_) => MockWindow());

    final result = await webDriverConnector.webDriversForHost('www.test.com');
    expect(result.single.currentUrl, 'https://www.test.com/path/2');
  });

  test('webDriversForHost no contexts', () async {
    mockAvailableWebDrivers(webDriverHelper, sl4f, portForwarder, {});
    final webDrivers =
        await webDriverConnector.webDriversForHost('www.test.com');
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

  test('TcpPortForwarder with proxyPort', () {
    when(sl4f.proxy).thenReturn(MockTcpProxyController());

    final forwarder = TcpPortForwarder(sl4f);
    expect(forwarder.proxyControl, sl4f.proxy);

    final forwarderWithProxyPort = TcpPortForwarder(sl4f, proxyPort: 1234);
    expect(forwarderWithProxyPort.proxyControl.proxyPorts, [1234]);
  });

  test('TcpPortForwarder with hostPort and targetHost', () async {
    final proxyController = MockTcpProxyController();
    when(proxyController.openProxy(8000)).thenAnswer((_) => Future.value(8001));
    when(sl4f.proxy).thenReturn(proxyController);
    when(sl4f.target).thenReturn('127.0.0.1');

    var forwarder = TcpPortForwarder(sl4f);
    var hostAndPort = await forwarder.forwardPort(8000);
    expect(hostAndPort.host, '127.0.0.1');
    expect(hostAndPort.port, 8001);

    forwarder = TcpPortForwarder(sl4f, hostPort: 1234);
    hostAndPort = await forwarder.forwardPort(8000);
    expect(hostAndPort.host, '127.0.0.1');
    expect(hostAndPort.port, 1234);

    forwarder = TcpPortForwarder(sl4f, hostPort: 1234, targetHost: 'localhost');
    hostAndPort = await forwarder.forwardPort(8000);
    expect(hostAndPort.host, 'localhost');
    expect(hostAndPort.port, 1234);
  });

  test('WebDriverHelper.createAsyncDriver uses custom HttpClient', () async {
    final mockClient = MockHttpClient();
    // Make sure the debugger is reachable so [createDriver] will be called.
    when(mockClient.getUrl(any))
        .thenAnswer((_) => Future.value(MockHttpClientRequest()));
    // This is from within [createDriver]. It's tricky to mock the happy path
    // completely, but we only need to verify this client is used.
    when(mockClient.postUrl(any))
        .thenAnswer((_) => Future.value(MockHttpClientRequest(success: false)));

    final webDriverHelper = WebDriverHelper(httpClient: mockClient);
    await expectLater(
        webDriverHelper.createAsyncDriver(HostAndPort('127.0.0.1', 8000),
            Uri.parse('http://127.0.0.1:1234/wd/hub/')),
        throwsA(isA<MockHttpException>()));
    verifyInOrder([
      mockClient.getUrl(Uri.parse('http://127.0.0.1:8000/')),
      mockClient.postUrl(Uri.parse('http://127.0.0.1:1234/wd/hub/session')),
    ]);
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

  when(webDriverHelper.createDriver(any, any)).thenAnswer((invocation) async {
    final accessPoint = invocation.positionalArguments.first;
    expect(accessPoint.host, equals(testDutAddress));
    final openPort = accessPoint.port;
    final remotePort = openPort - 10;
    WebDriver webDriver = MockWebDriver();
    when(webDriver.currentUrl).thenReturn(remotePortToUrl[remotePort]);
    return webDriver;
  });
}
