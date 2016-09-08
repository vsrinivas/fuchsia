// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:appengine/appengine.dart';
import 'package:cloud_indexer_common/wrappers.dart';
import 'package:gcloud/service_scope.dart' as ss;
import 'package:notification_handler/index_updater.dart';
import 'package:notification_handler/request_handler.dart' as rh;
import 'package:path/path.dart' as path;
import 'package:shelf/shelf_io.dart' as io;

initPubSubSubscription() async {
  final String topicName = Platform.environment['CLOUD_INDEXER_TOPIC_NAME'];

  // We construct a subscription name with respect to the topic name because
  // the topic might not necessarily be within the cloud indexer project.
  final String prefix = path.dirname(path.dirname(topicName));
  final String subscriptionName =
      '$prefix/subscriptions/${Platform.environment['GAE_MODULE_NAME']}';

  // Next, we delegate the choice of push endpoint to the request handler.
  final String pushEndpoint =
      'https://${Platform.environment['GAE_MODULE_NAME']}-dot-'
      '${Platform.environment['GAE_APPENGINE_HOSTNAME']}/'
      '${rh.messagePushEndpoint}';

  final PubSubTopicWrapper pubSubTopicWrapper =
      new PubSubTopicWrapper(authClientService, topicName);
  try {
    await pubSubTopicWrapper.createPushSubscription(
        subscriptionName, pushEndpoint);
  } on DetailedApiRequestError catch (e) {
    // If we receive 409, this means that the subscription already exists. In
    // this case, we can simply continue.
    if (e != HttpStatus.CONFLICT) throw e;
  }
}

main(List<String> args) {
  int port = 8080;
  if (args.length > 0) port = int.parse(args[0]);
  useLoggingPackageAdaptor();
  withAppEngineServices(() {
    return ss.fork(() async {
      await initPubSubSubscription();
      IndexUpdater indexUpdater = new IndexUpdater.fromClient(
          authClientService, Platform.environment['MODULE_BUCKET_NAME']);
      registerIndexUpdaterService(indexUpdater);
      return runAppEngine((HttpRequest request) {
        return io.handleRequest(request, rh.requestHandler);
      }, port: port);
    });
  });
}
