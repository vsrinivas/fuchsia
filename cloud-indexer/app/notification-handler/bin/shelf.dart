// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:isolate';
import 'dart:io';

import 'package:cloud_indexer_common/config.dart';
import 'package:cloud_indexer_common/wrappers.dart';
import 'package:gcloud/service_scope.dart' as ss;
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';
import 'package:notification_handler/index_updater.dart';
import 'package:notification_handler/request_handler.dart' as rh;
import 'package:path/path.dart' as path;
import 'package:shelf/shelf.dart' as shelf;
import 'package:shelf/shelf_io.dart' as io;

final Logger _logger = new Logger('notification_handler.shelf');

Future<Null> messageFetcher(int port) async {
  final Config config = await Config.create();

  final String topicName = config.topicName;
  final String prefix = path.dirname(path.dirname(topicName));
  final String subscriptionName = '$prefix/subscriptions/local-fetching';

  final http.Client client = config.cloudPlatformClient;
  final PubSubTopicWrapper topicWrapper =
      new PubSubTopicWrapper(client, topicName);

  try {
    await topicWrapper.createSubscription(subscriptionName);
  } on DetailedApiRequestError catch (e) {
    if (e.status != HttpStatus.CONFLICT) throw e;
  }

  while (true) {
    List<ReceivedMessage> messages =
        await topicWrapper.pull(subscriptionName, 5);
    for (ReceivedMessage message in messages) {
      await client.post('http://localhost:$port/${rh.messagePushEndpoint}',
          body: JSON.encode(message.toJson()));
      // Unlike production, we do not retry on failed messages.
      await topicWrapper.acknowledge([message.ackId], subscriptionName);
    }

    // We poll for new messages every second.
    sleep(new Duration(seconds: 1));
  }
}

main(List<String> args) {
  Logger.root.level = Level.ALL;
  Logger.root.onRecord.listen((LogRecord rec) {
    print('${rec.level.name}: ${rec.time}: ${rec.message}');
  });

  int port = 8080;
  if (args.length > 0) port = int.parse(args[0]);

  return ss.fork(() async {
    final Config config = await Config.create();
    registerConfigService(config);
    final IndexUpdater indexUpdater = new IndexUpdater.fromServiceScope();
    registerIndexUpdaterService(indexUpdater);
    io.serve(
        const shelf.Pipeline()
            .addMiddleware(shelf.logRequests())
            .addHandler(rh.requestHandler),
        'localhost',
        port);

    _logger.info('Notification handler is now servicing requests!');
    _logger.info('Setting up Isolate to poll for new messages.');
    ReceivePort receivePort = new ReceivePort();
    // We only use the receivePort in the context of an error.
    receivePort.listen((error) {
      _logger.warning('Received error from Isolate:\n$error');
      exit(1);
    });
    await Isolate.spawn(messageFetcher, port, onError: receivePort.sendPort);

    // We use this to prevent fall-through since io.serve completes immediately.
    return new Completer().future;
  });
}
