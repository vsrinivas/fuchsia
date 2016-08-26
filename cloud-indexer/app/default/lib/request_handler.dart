// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:gcloud/pubsub.dart';
import 'package:googleapis/pubsub/v1.dart' show DetailedApiRequestError;
import 'package:logging/logging.dart';
import 'package:shelf/shelf.dart' as shelf;

final Logger _logger = new Logger('cloud_indexer');
final RegExp _manifestRegExp =
    new RegExp(r'^services/([^/]+)/([^/]+)/[^/]+.yaml$');

const String _topicName =
    'projects/google.com:modular-cloud-indexer/topics/indexing';

Future<shelf.Response> requestHandler(shelf.Request request,
    {PubSub pubSub, String bucketName, String topicName: _topicName}) async {
  // Override variables from the service scope or environment depending on
  // whether they are provided as function arguments.
  pubSub ??= pubsubService;
  bucketName ??= Platform.environment['BUCKET_NAME'];

  if (request.method != 'POST') {
    return new shelf.Response.notFound(null);
  }

  final String resourceState = request.headers['X-Goog-Resource-State'];
  switch (resourceState) {
    case 'sync':
      _logger.info('Sync message received for URI: '
          '${request.headers['X-Goog-Resource-Uri']}');
      return new shelf.Response.ok(null);
    case 'not_exists':
    case 'exists':
      _logger.info('State change notification received.');
      final Map<String, dynamic> changeNotification = await request
          .read()
          .transform(UTF8.decoder)
          .transform(JSON.decoder)
          .single;

      if (changeNotification['bucket'] != bucketName) {
        _logger.warning('Invalid bucket name received. Bailing out.');
        return new shelf.Response.ok(null);
      }

      String name = changeNotification['name'];
      Match match = _manifestRegExp.matchAsPrefix(name);
      if (match == null) {
        _logger.info('Non-manifest file added. Bailing out.');
        return new shelf.Response.ok(null);
      }

      Map<String, String> result = {
        'name': name,
        'resource_state': resourceState,
        'arch': match.group(1),
        'revision': match.group(2)
      };

      try {
        _logger.info('Publishing manifest to be indexed: $name');
        Topic topic = await pubSub.lookupTopic(topicName);
        topic.publish(new Message.withString(JSON.encode(result)));
      } on DetailedApiRequestError catch (e) {
        _logger.warning(
            'Failed to publish to indexing topic (${e.status}): ${e.message}');
        return new shelf.Response.internalServerError();
      }

      return new shelf.Response.ok(null);
    default:
      _logger.info('Resource state $resourceState not supported.');
      return new shelf.Response(HttpStatus.BAD_REQUEST);
  }
}
