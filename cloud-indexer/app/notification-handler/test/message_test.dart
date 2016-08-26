// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:notification_handler/message.dart';
import 'package:test/test.dart';

import 'message_test_utils.dart';

main() {
  group('Message', () {
    const Map<String, String> testAttributes = const {'test': 'attributes'};
    const String testData = 'This is test data.';
    const String testMessageId = 'test_message_id';
    const String testSubscription = 'projects/subscriptions/test-subscription';

    test('Valid message.', () {
      final String jsonPushMessage = createJsonPushMessage(
          attributes: testAttributes,
          data: testData,
          messageId: testMessageId,
          subscription: testSubscription);

      Message message = new Message(jsonPushMessage);
      expect(message, isNotNull);
      expect(message.attributes, testAttributes);
      expect(message.data, testData);
      expect(message.messageId, testMessageId);
      expect(message.subscription, testSubscription);
    });

    test('Invalid JSON.', () {
      Message message = new Message('This is an invalid JSON string.');
      expect(message, isNull);
    });

    test('Missing fields.', () {
      Message message = new Message('{"subscriptions": "$testSubscription"}');
      expect(message, isNull);
    });
  });
}
