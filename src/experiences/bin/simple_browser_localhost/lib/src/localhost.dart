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
///   final url = await localhost.bindServer();
///   for(var file in yourWebFiles) {
///     localhost.passWebFiles(file);
///   }
///   localhost.startServing();
/// }
/// ```
/// See go/workstation_localhost for more details about using this library.
class Localhost {
  HttpServer _server;
  bool _isReady;
  final _pages = <String, File>{};

  Localhost() {
    setupLogger(name: 'simple_browser_localhost');
    _isReady = false;
  }

  /// Starts listening on the allocated url. The host address is usually
  /// 127.0.0.1 and the port number is 8080 by default. You can change the port
  /// number by passing it through the [port] parameter.
  Future<String> bindServer({int port = 8080}) async {
    var url = 'http://';
    try {
      _server = await HttpServer.bind(InternetAddress.loopbackIPv4, port);
      url += '${_server.address.address}:${_server.port}';
      log.info('Start serving on $url/');
      _isReady = true;
      //ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Failed to start the server: $e');
      url = '';
      _isReady = false;
    }
    return url;
  }

  /// Serves files that matches to the request. If `index.html` is requested,
  /// it finds the file in the [_pages] map with key, `index.html`, and serves it.
  /// If it cannot find the correspondant file in the map, it renders a plain
  /// text saying, 'Missing file: <file_name>'
  void startServing({int port = 8080}) async {
    assert(
      _isReady,
      'The server is not ready for serving files. Call bindServer() first.',
    );
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

  /// Saves the web files that [Localhost] will serve.
  ///
  /// It only supports html, css, and txt type files.
  /// The parameter [name] is the file name that the user will request via the
  /// url path. For example, if you want to access the file `example1_index.html`
  /// via the url `http://127.0.0.1:8080/index.html`, you have to call this
  /// method with this optional parameter: `passWebFile(File, name: 'index')`.
  // TODO(fxr/68321): Support media type files too.
  void passWebFile(File file, {bool replace = true}) {
    final fileName = file.path.split('/').last;
    final fileType = fileName.split('.').last;
    assert(
      ['html', 'css', 'txt'].any((type) => fileType == type),
      'The file type has to be one of html, css, and txt.',
    );

    _pages.update(
      fileName,
      (oldFile) {
        if (replace) {
          log.info('$fileName already exists. Replacing it with the new file.');
          return file;
        }
        log.info('$fileName already exists. Ignore the given file.');
        return oldFile;
      },
      ifAbsent: () {
        log.info('Now localhost can serve $fileName');
        return file;
      },
    );
  }

  /// Stops listening on http://127.0.0.1:<port_number>.
  void stopServer() async {
    setupLogger(name: 'simple_browser_localhost');
    try {
      await _server?.close();
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Failed to stop the server: $e');
    }
  }
}
