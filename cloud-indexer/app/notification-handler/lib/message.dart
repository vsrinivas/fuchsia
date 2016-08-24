// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

/// Represents a message requested by the Pub/Sub push service.
class Message {
  final Map<String, String> attributes;
  final String data;
  final String messageId;
  final String subscription;

  static bool _isValidPushRequest(Map<String, dynamic> pushRequest) =>
    pushRequest.containsKey('message') &&
    pushRequest['message'].containsKey('data') &&
    pushRequest['message'].containsKey('attributes') &&
    pushRequest['message'].containsKey('message_id') &&
    pushRequest.containsKey('subscription');

  Message._(this.attributes, this.data, this.messageId, this.subscription);

  factory Message(String pushRequest) {
    Map<String, dynamic> jsonPushRequest;
    try {
      jsonPushRequest = JSON.decode(pushRequest);
    } on FormatException {
      return null;
    }

    if (!_isValidPushRequest(jsonPushRequest)) {
      return null;
    }

    String encodedData = jsonPushRequest['message']['data'];
    String decodedData = UTF8.decode(BASE64.decode(encodedData));
    return new Message._(
        jsonPushRequest['message']['attributes'],
        decodedData,
        jsonPushRequest['message']['message_id'],
        jsonPushRequest['subscription']);
  }
}
