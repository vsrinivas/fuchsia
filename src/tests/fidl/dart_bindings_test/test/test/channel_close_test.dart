// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:test/test.dart';
import 'package:fidl/fidl.dart';

class TestAsyncBinding extends AsyncBinding<EmptyImpl> {
  // ignore: empty_constructor_bodies
  TestAsyncBinding() : super(r'TestAsyncBinding') {}
  @override
  void handleMessage(Message message, MessageSink respond) {}
}

class EmptyImpl {}

void main() {
  group('channel close', () {
    test('async bindings', () async {
      // Negative value to test correct handling of negative status codes.
      int expectedStatus = -100;
      EmptyImpl impl = EmptyImpl();
      Completer completer = Completer();
      AsyncProxyController<EmptyImpl> proxyController = AsyncProxyController()
        ..onEpitaphReceived = (int statusCode) {
          if (statusCode != expectedStatus) {
            completer.completeError(AssertionError(
                'status_code $statusCode did not match expected status code $expectedStatus'));
          } else {
            completer.complete();
          }
        };
      TestAsyncBinding()
        ..bind(impl, proxyController.request())
        ..close(expectedStatus);
      return completer.future;
    });
  });
}
