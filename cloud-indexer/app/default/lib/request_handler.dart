// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:logging/logging.dart';
import 'package:mime/mime.dart';
import 'package:shelf/shelf.dart' as shelf;

import 'module_uploader.dart';
import 'tarball.dart';

final Logger _logger = new Logger('cloud_indexer.request_handler');
final RegExp _boundaryRegExp =
    new RegExp(r'^.*boundary=(?:"([^"]+)"|([^\s"]+))$');

const String _namePattern = 'name="module"';

Future<shelf.Response> requestHandler(shelf.Request request,
    {ModuleUploader moduleUploader}) async {
  // Override variables from the service scope as default values.
  moduleUploader ??= moduleUploaderService;

  if (request.method != 'POST' || request.url.path != 'api/upload') {
    return new shelf.Response.notFound(null);
  }

  String contentType = request.headers['Content-Type'];
  if (!contentType.startsWith('multipart/form-data')) {
    // TODO(victorkwan): Update with identifying information once we have
    // authentication implemented. Here, and below.
    _logger.info('Invalid content-type received. Bailing out.');
    return new shelf.Response(HttpStatus.BAD_REQUEST);
  }

  Match boundaryMatch = _boundaryRegExp.matchAsPrefix(contentType);
  if (boundaryMatch == null) {
    _logger.info('Invalid boundary received. Bailing out.');
    return new shelf.Response(HttpStatus.BAD_REQUEST,
        body: 'Invalid boundary.');
  }

  String boundary = boundaryMatch.group(1) ?? boundaryMatch.group(2);
  Stream<MimeMultipart> parts =
      request.read().transform(new MimeMultipartTransformer(boundary));
  await for (MimeMultipart part in parts) {
    String contentDisposition = part.headers['content-disposition'];
    if (!contentDisposition.contains(_namePattern)) continue;

    try {
      await moduleUploader.processUpload(part);
      return new shelf.Response.ok(null);
    } on CloudStorageException {
      return new shelf.Response.internalServerError();
    } on PubSubException {
      return new shelf.Response.internalServerError();
    } on TarballException catch (e) {
      return new shelf.Response(HttpStatus.BAD_REQUEST, body: e.toString());
    }
  }

  _logger.info('Request did not contain a tarball. Bailing out.');
  return new shelf.Response(HttpStatus.BAD_REQUEST, body: 'Missing tarball.');
}
