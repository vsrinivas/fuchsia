// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  MockSl4f sl4f;
  WebDriverConnector webDriverConnector;

  setUp(() {
    sl4f = MockSl4f();
    when(sl4f.target).thenReturn('[::1]');
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
