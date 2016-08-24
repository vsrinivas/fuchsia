// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' show HttpStatus;

import 'package:cloud_indexer/request_handler.dart';
import 'package:gcloud/pubsub.dart';
import 'package:googleapis/pubsub/v1.dart' show DetailedApiRequestError;
import 'package:mockito/mockito.dart';
import 'package:shelf/shelf.dart' as shelf;
import 'package:test/test.dart';

class MockPubSub extends Mock implements PubSub {}

class MockTopic extends Mock implements Topic {}

String createChangeNotification(String name, String bucket) =>
    JSON.encode({'name': name, 'bucket': bucket});

main() {
  group('requestHandler', () {
    const String testArch = 'test_arch';
    const String testRevision = 'test_revision';
    const String testName = 'services/$testArch/$testRevision/test_module.yaml';
    const String testBucketName = 'test-modular-bucket';
    const String testTopicName = 'projects/topics/test-modular-topic';

    final Uri defaultUri = Uri.parse('https://default-service.io/');

    test('Invalid resource state.', () async {
      PubSub pubSub = new MockPubSub();
      shelf.Request request = new shelf.Request('POST', defaultUri);
      shelf.Response response = await requestHandler(request,
          pubSub: pubSub, bucketName: testBucketName, topicName: testTopicName);

      // The object change notification service should resend the notification.
      expect(response.statusCode, greaterThan(299));
    });

    test('Sync request.', () async {
      PubSub pubSub = new MockPubSub();
      shelf.Request request = new shelf.Request('POST', defaultUri, headers: {
        'X-Goog-Resource-State': 'sync',
        'X-Goog-Resource-Uri': 'https://resource-uri.io/'
      });
      shelf.Response response = await requestHandler(request,
          pubSub: pubSub, bucketName: testBucketName, topicName: testTopicName);

      expect(response.statusCode, HttpStatus.OK);
    });

    test('Invalid bucket.', () async {
      PubSub pubSub = new MockPubSub();
      shelf.Request request = new shelf.Request('POST', defaultUri,
          headers: {'X-Goog-Resource-State': 'exists'},
          body: createChangeNotification(testName, 'non-modular-bucket'));
      shelf.Response response = await requestHandler(request,
          pubSub: pubSub, bucketName: testBucketName, topicName: testTopicName);

      // We want to let the notification pass.
      expect(response.statusCode, HttpStatus.OK);
    });

    test('Invalid name.', () async {
      PubSub pubSub = new MockPubSub();

      // The name is invalid because it is not a yaml manifest.
      shelf.Request request = new shelf.Request('POST', defaultUri,
          headers: {'X-Goog-Resource-State': 'exists'},
          body: createChangeNotification(
              'services/test_arch/test_revision/test_module.json',
              testBucketName));
      shelf.Response response = await requestHandler(request,
          pubSub: pubSub, bucketName: testBucketName, topicName: testTopicName);

      // We want to let the notification pass.
      expect(response.statusCode, HttpStatus.OK);
    });

    test('Valid request.', () async {
      PubSub pubSub = new MockPubSub();
      Topic topic = new MockTopic();
      when(pubSub.lookupTopic(testTopicName)).thenReturn(topic);

      shelf.Request request = new shelf.Request('POST', defaultUri,
          headers: {'X-Goog-Resource-State': 'exists'},
          body: createChangeNotification(testName, testBucketName));
      shelf.Response response = await requestHandler(request,
          pubSub: pubSub, bucketName: testBucketName, topicName: testTopicName);

      Message message = verify(topic.publish(captureAny)).captured[0];
      Message expectedMessage = new Message.withString(JSON.encode({
        'name': testName,
        'arch': testArch,
        'revision': testRevision,
        'resource_state': 'exists'
      }));
      expect(
          JSON.decode(message.asString), JSON.decode(expectedMessage.asString));

      expect(response.statusCode, HttpStatus.OK);
    });

    test('Valid request, but Pub/Sub request error.', () async {
      PubSub pubSub = new MockPubSub();
      when(pubSub.lookupTopic(testTopicName)).thenAnswer((i) =>
          throw new DetailedApiRequestError(
              HttpStatus.INTERNAL_SERVER_ERROR, 'Internal server error.'));

      shelf.Request request = new shelf.Request('POST', defaultUri,
          headers: {'X-Goog-Resource-State': 'exists'},
          body: createChangeNotification(testName, testBucketName));
      shelf.Response response = await requestHandler(request,
          pubSub: pubSub, bucketName: testBucketName, topicName: testTopicName);

      expect(response.statusCode, greaterThan(299));
    });
  });
}
