// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io' show HttpStatus;

import 'package:cloud_indexer_common/wrappers.dart';
import 'package:logging/logging.dart';
import 'package:notification_handler/index_updater.dart';
import 'package:shelf/shelf.dart' as shelf;

final Logger _logger = new Logger('notification_handler.request_handler');

const String messagePushEndpoint = '_ah/push-handlers/indexing';

/// Handles a Pub/Sub push message.
///
/// Currently, we assume that there is no notion of deleting a module: once a
/// manifest is removed, we have no means of determining the URL the module is
/// indexed against.
Future<shelf.Response> requestHandler(shelf.Request request,
    {IndexUpdater indexUpdater}) async {
  // In the case that an IndexUpdater is not provided, we use the one registered
  // to the current service scope.
  indexUpdater ??= indexUpdaterService;

  // TODO(victorkwan): Provide authentication checks once these are available
  // on Managed VMs: https://code.google.com/p/cloud-pubsub/issues/detail?id=32
  if (request.method != 'POST' || request.url.path != messagePushEndpoint) {
    return new shelf.Response.notFound(null);
  }

  PubsubMessage message;
  try {
    message = new ReceivedMessage.fromJson(await request
            .read()
            .transform(UTF8.decoder)
            .transform(JSON.decoder)
            .single)
        .message;
  } on FormatException {
    _logger.info('Failed to decode Pub/Sub message.');
    return new shelf.Response(HttpStatus.BAD_REQUEST);
  }

  try {
    _logger.info('Starting manifest update.');
    await indexUpdater.update(UTF8.decode(message.dataAsBytes));
  } on AtomicUpdateFailureException {
    return new shelf.Response.internalServerError();
  } on CloudStorageFailureException {
    return new shelf.Response.internalServerError();
  } on ManifestException {
    // In the case that the manifest is malformed or non-existent, we should
    // not attempt to retry updating the index.
    return new shelf.Response.ok(null);
  }

  _logger.info('Finished manifest update.');
  return new shelf.Response.ok(null);
}
