// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_webview_flutter/webview.dart';
import 'package:webview_flutter/webview_flutter.dart';

// ignore_for_file: implementation_imports

const kPageHtml = '''
<html>
  <head>
    <title>My test page</title>
  </head>
  <body>
    Test
  </body>
</html>
''';

const kSampleScript = '''
(function() {
  TestChannel.postMessage('test succeeded');
})();
''';

void main() {
  setUpAll(() async {
    WebView.platform = FuchsiaWebView();
  });

  testWidgets('loading', (WidgetTester tester) async {
    final base64Content = base64Encode(Utf8Encoder().convert(kPageHtml));
    final url = 'data:text/html;base64,$base64Content';
    late WebViewController webViewController;
    final webView = WebView(
      onWebViewCreated: (WebViewController controller) {
        webViewController = controller;
      },
      initialUrl: url,
    );

    await tester.pumpWidget(webView);
    expect(await webViewController.currentUrl(), url);
    expect(await webViewController.getTitle(), 'My test page');
  });

  testWidgets('javascript object', (WidgetTester tester) async {
    final base64Content = base64Encode(Utf8Encoder().convert(kPageHtml));
    final url = 'data:text/html;base64,$base64Content';
    var success = false;
    late WebViewController webViewController;
    final webView = WebView(
      onWebViewCreated: (WebViewController controller) {
        webViewController = controller;
      },
      initialUrl: url,
      // Reason: sdk_version_set_literal unsupported until version 2.2
      // ignore: prefer_collection_literals
      javascriptChannels: Set.from([
        JavascriptChannel(
          name: 'TestChannel',
          onMessageReceived: (JavascriptMessage message) {
            success = message.message == 'test succeeded';
          },
        ),
      ]),
    );

    await tester.pumpWidget(webView);
    await webViewController.runJavascript(kSampleScript);
    expect(success, true);
  });
}
