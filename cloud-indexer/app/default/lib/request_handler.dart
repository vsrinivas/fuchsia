// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:logging/logging.dart';
import 'package:shelf/shelf.dart' as shelf;

import 'auth_manager.dart';
import 'module_uploader.dart';
import 'zip.dart';

final Logger _logger = new Logger('cloud_indexer.request_handler');

Future<shelf.Response> requestHandler(shelf.Request request,
    {ModuleUploader moduleUploader, AuthManager authManager}) async {
  // Override variables from the service scope as default values.
  moduleUploader ??= moduleUploaderService;
  authManager ??= authManagerService;

  if (!await authManager.checkAuthenticated(request.headers['Authorization'])) {
    return new shelf.Response.forbidden(null);
  }

  if (request.method != 'POST' || request.url.path != 'api/upload') {
    return new shelf.Response.notFound(null);
  }

  final String contentType = request.headers['Content-Type'];
  if (contentType == null || !contentType.startsWith('application/zip')) {
    _logger.info('Invalid content-type received. Bailing out.');
    return new shelf.Response(HttpStatus.BAD_REQUEST);
  }

  try {
    await moduleUploader.processUpload(request.read());
    return new shelf.Response.ok(null);
  } on CloudStorageException {
    return new shelf.Response.internalServerError();
  } on PubSubException {
    return new shelf.Response.internalServerError();
  } on ZipException catch (e) {
    return new shelf.Response(HttpStatus.BAD_REQUEST, body: e.toString());
  }
}
