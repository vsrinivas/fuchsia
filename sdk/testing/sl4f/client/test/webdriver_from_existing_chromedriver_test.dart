// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart';

class MockSsh extends Mock implements Ssh {}

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  MockSl4f sl4f;
  MockSsh ssh;
  WebDriverConnector webDriverConnector;

  setUp(() {
    sl4f = MockSl4f();
    ssh = MockSsh();
    when(sl4f.ssh).thenReturn(ssh);
    webDriverConnector =
        WebDriverConnector.fromExistingChromedriver(1234, sl4f);
  });

  test('webDriverConnector does not have a ChromeDriver process', () {
    expect(webDriverConnector.chromedriverProcess, null);
  });
  test('initialize() does not start another ChromeDriver process', () async {
    await webDriverConnector.initialize();
    expect(webDriverConnector.chromedriverProcess, null);
  });
}
