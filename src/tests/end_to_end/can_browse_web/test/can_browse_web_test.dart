// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:webdriver/sync_io.dart' show By, WebDriver;

const _timeout = Duration(seconds: 180);
const _chromeDriverPath = 'runtime_deps/chromedriver';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Dump dump;
  Directory dumpDir;
  sl4f.Performance performance;
  sl4f.WebDriverConnector webDriverConnector;

  setUpAll(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    webDriverConnector = sl4f.WebDriverConnector(_chromeDriverPath, sl4fDriver);
    await webDriverConnector.initialize();
  });

  tearDownAll(() async {
    await webDriverConnector.tearDown();

    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  setUp(() async {
    await sl4f.Modular(sl4fDriver).restartSession();
    dumpDir = await Directory.systemTemp.createTemp('temp-dump');
    dump = sl4f.Dump(dumpDir.path);
    performance = sl4f.Performance(sl4fDriver, dump);
  });

  tearDown(() async {
    await sl4f.Modular(sl4fDriver).restartSession();
    dumpDir.deleteSync(recursive: true);
  });

  group(sl4f.Sl4f, () {
    test('click link and trace', () async {
      sl4f.Scenic scenicDriver = sl4f.Scenic(sl4fDriver);
      await scenicDriver.takeScreenshot(dumpName: 'start-of-test');
      await sl4fDriver.ssh.run('sessionctl add_mod https://fuchsia.dev');

      // Wait for the browser and page to load.
      await Future.delayed(Duration(seconds: 10));

      await scenicDriver.takeScreenshot(dumpName: 'browser-started');

      final debuggerUrl = (await webDriverConnector
              .webSocketDebuggerUrlsForHost('fuchsia.dev',
                  filters: {'type': 'page'}))
          .single;
      final traceWebsocket = await performance.startChromeTrace(debuggerUrl);

      WebDriver webdriver =
          (await webDriverConnector.webDriversForHost('fuchsia.dev')).single;
      final termsLink = webdriver.findElement(By.linkText('Terms'));
      expect(termsLink, isNotNull, reason: 'Cannot find Terms link');
      termsLink.click();

      await Future.delayed(Duration(seconds: 5));
      final traceFile = await performance.stopChromeTrace(traceWebsocket,
          traceName: 'fuchsia-dev');

      final traceData = json.decode(await traceFile.readAsString());
      expect(traceData, isList);
      expect(traceData, isNotEmpty);
    });
  }, timeout: Timeout(_timeout));
}
