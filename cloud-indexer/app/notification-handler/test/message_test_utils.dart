// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

String createJsonPushMessage(
    {Map<String, String> attributes: const {},
    String data: '',
    String messageId: '',
    String subscription: ''}) {
  return JSON.encode({
    'message': {
      'attributes': attributes,
      'data': BASE64.encode(data.codeUnits),
      'message_id': messageId,
    },
    'subscription': subscription
  });
}
