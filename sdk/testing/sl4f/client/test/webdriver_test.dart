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
    when(sl4f.ssh).thenReturn(MockSsh());
    processHelper = MockProcessHelper();
    webDriverHelper = MockWebDriverHelper();
    webDriverConnector = WebDriverConnector('path/to/chromedriver', sl4f,
        processHelper: processHelper, webDriverHelper: webDriverHelper);
  });

  test('webDriversForHost filters by host', () async {
    var openContexts = {
      20000: 'https://www.test.com/path/1',
      20001: 'https://www.example.com/path/1',
      20002: 'https://www.test.com/path/2',
      20003: 'https://www.example.com/path/2'
    };
    mockAvailableWebDrivers(webDriverHelper, sl4f, openContexts);
    var webDrivers = await webDriverConnector.webDriversForHost('www.test.com');
    expect(webDrivers.length, 2);
    var webDriverCurrentUrls =
        Set.from(webDrivers.map((webDriver) => webDriver.currentUrl));
    expect(webDriverCurrentUrls,
        {'https://www.test.com/path/1', 'https://www.test.com/path/2'});
  });

  test('webDriversForHost no contexts', () async {
    mockAvailableWebDrivers(webDriverHelper, sl4f, {});
    var webDrivers = await webDriverConnector.webDriversForHost('www.test.com');
    expect(webDrivers.length, 0);
  });
}

/// Set up mocks as if there are chrome contexts with the given ports exposing a url.
void mockAvailableWebDrivers(MockWebDriverHelper webDriverHelper, MockSl4f sl4f,
    Map<int, String> portToUrl) {
  var portList = {'ports': List.from(portToUrl.keys)};
  print(portList);
  when(sl4f.request('webdriver_facade.GetDevToolsPorts'))
      .thenAnswer((_) => Future.value(portList));
  when(webDriverHelper.createDriver(any, any)).thenAnswer((invocation) {
    var devToolsPort = invocation.positionalArguments.first;
    WebDriver webDriver = MockWebDriver();
    when(webDriver.currentUrl).thenReturn(portToUrl[devToolsPort]);
    return webDriver;
  });
}
