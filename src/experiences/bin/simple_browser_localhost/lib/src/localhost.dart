// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'package:fuchsia_logger/logger.dart';

/// An object that serves a static local website for testing purpose.
///
/// [startServer] starts serving to http://127.0.0.1:<port_number>
/// The default <port_number> is 8080.
///
/// You can pass a list of files that you want to render to your [Localhost]
/// using [passWebFiles]. It only accepts html, css, and plain text files as
/// [Localhost] can only render those types of content.
///
/// [stopServer] stops serving to http://127.0.0.1:<port_number>.
///
/// Following is the example code that shows how to use this library.
///
/// Add this dependency in your BUILD:
/// `//src/experiences/bin/simple_browser_localhost:simple_browser_localhost`
///
/// In your code where you want to use this library:
/// ```
/// import 'package:simple_browser_localhost/localhost.dart';
///
/// void main() {
///   final webFiles = [File('path_to_your_html'), File(fil), File()];
///   final localhost = Localhost();
///   setUp(() async {
///     localhost.startServer();
///     localhost.passWebFiles(webFiles);
///   });
///
///   tearDownAll(() async {
///     localhost.stopServer();
///   });
/// }
/// ```
class Localhost {
  HttpServer _server;
  final _pages = <String, File>{};

  Localhost() {
    setupLogger(name: 'simple_browser_localhost');
  }

  void startServer({int port = 8080}) async {
    try {
      _server = await HttpServer.bind(InternetAddress.loopbackIPv4, port);
      log.info(
          'Start serving on http://${_server.address.address}:${_server.port}/');
      //ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Failed to start the server: $e');
    }

    await for (var req in _server) {
      final fileName = req.uri.path.substring(1); // eleminates '/'
      final fileType = fileName.split('.').last;
      final targetFile = _pages[fileName];

      if (targetFile != null && targetFile.existsSync()) {
        log.info('Serving $fileName');
        switch (fileType) {
          case 'html':
            req.response.headers.contentType = ContentType.html;
            break;
          case 'css':
            req.response.headers.contentType = ContentType.parse('text/css');
            break;
          case 'txt':
            req.response.headers.contentType = ContentType.text;
            break;
          default:
            break;
        }
        try {
          await req.response.addStream(targetFile.openRead());
          //ignore: avoid_catches_without_on_clauses
        } catch (e) {
          log.shout('Something went wrong when reading the file: $e');
        }
      } else {
        log.warning('No such file $fileName. '
            'Check if you passed necessary files to your Localhost using '
            'passWebFiles().');
        req.response
          ..headers.contentType = ContentType.text
          ..write('Missing file: $fileName');
      }
      await req.response.close();
    }
  }

  void passWebFiles(List<File> files) {
    for (var file in files) {
      final fileName = file.path.split('/').last;
      final fileType = fileName.split('.').last;
      assert(['html', 'css', 'txt'].any((type) => fileType == type));

      _pages.putIfAbsent(fileName, () => file);
      log.info('Now localhost can serve $fileName');
    }
  }

  void stopServer() async {
    setupLogger(name: 'simple_browser_localhost');
    try {
      await _server.close();
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Failed to stop the server: $e');
    }
  }
}
