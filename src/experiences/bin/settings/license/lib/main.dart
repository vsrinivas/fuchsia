// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_webview_flutter/webview.dart';
import 'package:webview_flutter/webview_flutter.dart';

import 'src/license.dart';

const _kErrorPage = '''
<html>
  <body style="background-color:#ffffff;">
    There was a problem loading the license file.
  </body>
</html>
''';

/// Main entry point to the license settings module
void main() async {
  setupLogger(name: 'license_settings');

  String url = 'http://';
  const path = 'pkg/data/license.html';
  File file = File(path);

  if (file.existsSync()) {
    try {
      await HttpServer.bind(InternetAddress.loopbackIPv4, 0).then((server) {
        server.listen((HttpRequest request) {
          request.response.headers.contentType = ContentType.parse('text/html');
          request.response.addStream(file.openRead());
          request.response.close();
        });
        url += '${server.address.address}:${server.port}';
      });
    } on Exception catch (e) {
      log.shout('Failed to start the server: $e');
      final base64Content = base64Encode(Utf8Encoder().convert(_kErrorPage));
      url = 'data:text/html;base64,$base64Content';
    }
  } else {
    log.warning('No such file or directory: ${file.path}');
    final base64Content = base64Encode(Utf8Encoder().convert(_kErrorPage));
    url = 'data:text/html;base64,$base64Content';
  }

  // Sets the default web view as [FuchsiaWebView].
  WebView.platform = FuchsiaWebView.create();

  runApp(License(url));
}
