// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:mockito/mockito.dart';
import 'package:notification_handler/index_updater.dart';
import 'package:notification_handler/request_handler.dart';
import 'package:shelf/shelf.dart' as shelf;
import 'package:test/test.dart';

import 'message_test_utils.dart';

class MockIndexUpdater extends Mock implements IndexUpdater {}

main() {
  group('requestHandler', () {
    final Uri testUri = Uri.parse('https://test-notification-handler.io/');
    const String testArch = 'test_arch';
    const String testRevision = 'test_revision';

    const String testSubscription =
        'projects/subscriptions/test-modular-subscription';
    const String testMessageId = 'test_message_id';

    const String jsonManifest = '{'
        '"title":"Test Entry",'
        '"url":"https://test.io/module.mojo",'
        '"icon":"https://test.io/icon.png",'
        '"themeColor":null,'
        '"use":{},'
        '"verb":'
        '    {"label":{"uri":"https://seek.io","shorthand":"https://seek.io"}},'
        '"input":[],'
        '"output":[],'
        '"compose":[],'
        '"display":[],'
        '"schemas":[],'
        '"arch":"$testArch",'
        '"modularRevision":"$testRevision"'
        '}';

    const String malformedJsonManifest = '{'
        '"title":"Test Entry",'
        '"url":"https://test.io/module.mojo",'
        '"icon":"https://test.io/icon.png",'
        '"themeColor":null,'
        '"use":{},'
        '"verb":'
        '    {"label":{"uri":"https://seek.io","shorthand":"https://seek.io"}},'
        '"input":[],'
        '"output":[],'
        '"compose":[],'
        '"display":[],'
        '"schemas":[],'
        '"arch":"$testArch",'
        '"modularRevision":"$testRevision"'
        '}';

    test('Invalid push message.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      shelf.Request request = new shelf.Request('POST', testUri);
      shelf.Response response = await requestHandler(request,
          indexUpdater: indexUpdater, subscriptionName: testSubscription);
      expect(response.statusCode, greaterThan(299));
    });

    test('Invalid subscription.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: createJsonPushMessage(
              data: jsonManifest,
              messageId: testMessageId,
              subscription: 'projects/subscriptions/not-indexing'));
      shelf.Response response = await requestHandler(request,
          indexUpdater: indexUpdater, subscriptionName: testSubscription);
      expect(response.statusCode, HttpStatus.OK);
    });

    test('Atomic guarantee failure.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      when(indexUpdater.update(jsonManifest)).thenAnswer((i) =>
          throw new AtomicUpdateFailureException('Atomic guarantees failed.'));
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: createJsonPushMessage(
              data: jsonManifest,
              messageId: testMessageId,
              subscription: testSubscription));
      shelf.Response response = await requestHandler(request,
          indexUpdater: indexUpdater, subscriptionName: testSubscription);
      expect(response.statusCode, greaterThan(299));
    });

    test('Cloud storage failure.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      when(indexUpdater.update(jsonManifest)).thenAnswer((i) =>
          throw new CloudStorageFailureException(
              'Internal server error.', HttpStatus.INTERNAL_SERVER_ERROR));
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: createJsonPushMessage(
              data: jsonManifest,
              messageId: testMessageId,
              subscription: testSubscription));
      shelf.Response response = await requestHandler(request,
          indexUpdater: indexUpdater, subscriptionName: testSubscription);
      expect(response.statusCode, greaterThan(299));
    });

    test('Manifest exception.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      when(indexUpdater.update(malformedJsonManifest)).thenAnswer(
          (i) => throw new ManifestException('Malformed manifest.'));
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: createJsonPushMessage(
              data: malformedJsonManifest,
              messageId: testMessageId,
              subscription: testSubscription));
      shelf.Response response = await requestHandler(request,
          indexUpdater: indexUpdater, subscriptionName: testSubscription);
      expect(response.statusCode, HttpStatus.OK);
    });

    test('Valid notification.', () async {
      IndexUpdater indexUpdater = new MockIndexUpdater();
      when(indexUpdater.update(jsonManifest))
          .thenReturn(new Future.value(null));
      shelf.Request request = new shelf.Request('POST', testUri,
          headers: {'X-AppEngine-QueueName': 'indexing'},
          body: createJsonPushMessage(
              data: jsonManifest,
              messageId: testMessageId,
              subscription: testSubscription));
      shelf.Response response = await requestHandler(request,
          indexUpdater: indexUpdater, subscriptionName: testSubscription);
      expect(response.statusCode, HttpStatus.OK);
    });
  });
}
