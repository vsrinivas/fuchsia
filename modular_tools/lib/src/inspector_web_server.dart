// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:path/path.dart' as p;

import 'configuration.dart';
import 'refresh.dart';

const Map<String, String> _extensionMimeType = const {
  '.html': 'text/html',
  '.css': 'text/css',
  '.js': 'application/javascript',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.json': 'application/json',
  '.txt': 'text/plain',
  '.ico': 'image/vnd.microsoft.icon',
};

class InspectorWebServer {
  final int port;
  final String inspectorRoot;
  final String modularRoot;

  // Following members are needed for reloading module.
  final EnvironmentConfiguration _environment;
  final bool _release;

  InspectorWebServer(String modularRoot, this._environment, this._release,
      {this.port: 8000})
      : inspectorRoot = p.normalize(p.absolute(modularRoot + '/inspector')),
        modularRoot = p.normalize(p.absolute(modularRoot)) {}

  Future<Null> run() async {
    HttpServer requestServer =
        await HttpServer.bind(InternetAddress.LOOPBACK_IP_V4, port);
    print('Inspector listening http://localhost:${requestServer.port}/');
    await for (HttpRequest request in requestServer) {
      if (request.method == 'POST') {
        _handlePostRequest(request);
        continue;
      }
      // Only allow GET requests. This is just for serving static content.
      if (request.method != 'GET') {
        request.response.statusCode = HttpStatus.METHOD_NOT_ALLOWED;
        request.response.close();
        continue;
      }

      // Strip off the leading / from the request path.
      String requestPath = request.uri.path.substring(1);
      if (requestPath.isEmpty) {
        // For requests for the root, look for index.html
        requestPath = 'index.html';
      }

      // Calculate the local filesystem path for this file.
      String localPath = p.normalize(p.join(inspectorRoot, requestPath));
      // Make sure it still falls within the file tree.
      if (!p.isWithin(inspectorRoot, localPath)) {
        request.response.statusCode = HttpStatus.BAD_REQUEST;
        request.response.close();
        continue;
      }

      // Look for the actual file, make sure it's a file and exists.
      final File localFile = new File(localPath);
      if (!(await localFile.exists()) ||
          (await localFile.stat()).type != FileSystemEntityType.FILE) {
        request.response.statusCode = HttpStatus.NOT_FOUND;
        request.response.close();
        continue;
      }

      // Serve the appropriate Content-Type header.
      final String extension = p.extension(localPath).toLowerCase();
      if (_extensionMimeType.containsKey(extension)) {
        request.response.headers
            .set('content-type', _extensionMimeType[extension]);
      } else {
        request.response.headers
            .set('content-type', 'application/octet-stream');
      }
      request.response.headers.set('content-length', await localFile.length());

      // Send the file.
      Stream<List<int>> fileStream = localFile.openRead();
      request.response.addStream(fileStream).then((_) {
        request.response.close();
      });
    }
  }

  void _handlePostRequest(HttpRequest request) {
    // Strip off the leading / from the request path.
    if (request.uri.path == '/refresh') {
      _refreshModule(request.uri.queryParameters['url']).whenComplete(() {
        request.response.statusCode = HttpStatus.NO_CONTENT;
        request.response.close();
      });
    }
  }

  // Refreshes all instances of the module with latest binary updates.
  Future<int> _refreshModule(String moduleUrl) {
    final String currentFileName = Uri.parse(moduleUrl).pathSegments.last;
    final List<String> nameParts = currentFileName.split('.');
    // Check that name parts are less than or equal to 3.
    // One for the name, one for hash, one for the extension.
    assert(nameParts.length < 4);
    // Get output name stripping the hash.
    final String outputName =
        [nameParts[0], nameParts[nameParts.length - 1]].join('.');

    return new RefreshCommandRunner(_environment, _release).refresh(outputName);
  }
}
