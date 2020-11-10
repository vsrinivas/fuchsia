// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';
import 'package:test/test.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';

import './server.dart';

void main() async {
  TestServerInstance server;

  group('events', () {
    setUpAll(() async {
      server = TestServerInstance();
      await server.start();
    });

    tearDownAll(() async {
      await server.stop();
      server = null;
    });

    test('empty', () async {
      await server.proxy.sendEmptyEvent();
      expect(server.proxy.emptyEvent.first, completes);
    });

    test('string arg', () async {
      await server.proxy.sendStringEvent('Hello, world');
      expect(
          server.proxy.stringEvent.first, completion(equals('Hello, world')));
    });

    test('three args', () async {
      final primes =
          Uint8List.fromList([2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]);
      await server.proxy.sendThreeArgEvent(
          23, 42, NoHandleStruct(foo: 'hello', bar: 1729, baz: primes));
      final threeReply = await server.proxy.threeArgEvent.first;
      expect(threeReply.x, equals(23));
      expect(threeReply.y, equals(42));
      expect(threeReply.z.foo, equals('hello'));
      expect(threeReply.z.bar, equals(1729));
      expect(threeReply.z.baz, unorderedEquals(primes));
    });

    test('multiple events', () async {
      expect(server.proxy.multipleEvent, emitsInOrder([1, 2, 3, 4, 5]));
      await server.proxy.sendMultipleEvents(5, 0.02);
    });
  });
}
