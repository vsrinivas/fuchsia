// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:mime/mime.dart';

import 'src/app.dart';

void main(List<String> args) async {
  setupLogger(name: 'ermine_testserver');

  String _getPath(String fileName) => '/pkg/data/simple_browser_test/$fileName';

  log.info('ermine_testserver is running.');

  await HttpServer.bind(InternetAddress.loopbackIPv4, 8080).then((server) {
    server.listen((HttpRequest request) {
      final fileName = request.uri.path.substring(1); // eleminates '/'
      final mimeType = lookupMimeType(fileName);
      try {
        if (mimeType == null) {
          log.warning('The server has no such file: $fileName');
          request.response
            ..statusCode = 404
            ..close();
        } else {
          final file = File(_getPath(fileName));
          if (file.existsSync()) {
            log.info('Serving $fileName');
            request.response
              ..headers.contentType = ContentType.parse(mimeType)
              ..addStream(file.openRead())
                  .then((_) => request.response.close());
          } else {
            log.warning('Failed to load $fileName');
            request.response
              ..statusCode = 404
              ..close();
          }
        }
      } on Exception catch (e) {
        log.severe('$e: Failed to response on ${request.uri.path}');
      }
    });
  });

  runApp(App());
}
