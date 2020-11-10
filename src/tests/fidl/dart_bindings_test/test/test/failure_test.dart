// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:test/test.dart';

import './server.dart';

void main() async {
  TestServerInstance server;
  group('failure', () {
    setUp(() async {
      server = TestServerInstance();
      await server.start();
    });

    tearDown(() async {
      if (server != null) {
        await server.stop();
      }
      server = null;
    });

    test('remote close during call', () async {
      expect(server.proxy.replySlowly('hey man', 1.0), throwsA(anything));
      return server.proxy.closeConnection(0.1);
    });

    test('remote close during call with epitaph', () async {
      int expectedStatus = -100;
      Completer completer = Completer();
      server.proxy.ctrl.onEpitaphReceived = (int statusCode) {
        if (statusCode != expectedStatus) {
          completer.completeError(AssertionError(
              'status_code $statusCode did not match expected status code $expectedStatus'));
        } else {
          completer.complete();
        }
      };
      expect(server.proxy.replySlowly('hey man', 1.0), throwsA(anything));
      return server.proxy
          .closeConnectionWithEpitaph(expectedStatus, 0.1)
          .then((val) => completer);
    });

    test('local close during call', () async {
      expect(server.proxy.replySlowly('whoa dude', 1.0), throwsA(anything));
      server.proxy.ctrl.close();
    });

    test('server killed during call', () async {
      expect(server.proxy.replySlowly('whoa dude', 1.0), throwsA(anything));
      return server.controller.kill();
    });

    test('one-way call on closed proxy', () {
      server.proxy.ctrl.close();
      expect(server.proxy.oneWayNoArgs(), throwsA(anything));
    });

    test('two-way call on closed proxy', () {
      server.proxy.ctrl.close();
      expect(server.proxy.twoWayNoArgs(), throwsA(anything));
    });

    test('listen for events on a closed proxy', () {
      server.proxy.ctrl.close();
      expect(server.proxy.emptyEvent.first, throwsA(anything));
    });

    test('proxy closes while listening for events', () {
      expect(server.proxy.emptyEvent.first, throwsA(anything));
      server.proxy.ctrl.close();
    });
  });

  group('unbound', () {
    test('one-way call on unbound proxy', () {
      final proxy = TestServerProxy();
      expect(proxy.oneWayNoArgs(), throwsA(anything));
    });
    test('two-way call on unbound proxy', () {
      final proxy = TestServerProxy();
      expect(proxy.twoWayNoArgs(), throwsA(anything));
    });
    test('event listen on unbound proxy', () {
      final proxy = TestServerProxy();
      expect(proxy.emptyEvent.first, doesNotComplete);
    });
  });
}
