// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:typed_data';

import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:server_test/server.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart' show System;

void main() async {
  TestServerInstance server;
  group('two way', () {
    setUpAll(() async {
      server = TestServerInstance();
      await server.start();
    });

    tearDownAll(() async {
      await server.stop();
      server = null;
    });

    test('no args', () {
      expect(server.proxy.twoWayNoArgs(), completes);
    });

    test('string arg', () {
      expect(server.proxy.twoWayStringArg('Hello, world'),
          completion(equals('Hello, world')));
    });

    test('three args', () async {
      final primes =
          Uint8List.fromList([2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]);

      final threeReply = await server.proxy.twoWayThreeArgs(
          23, 42, NoHandleStruct(foo: 'hello', bar: 1729, baz: primes));

      expect(threeReply.x, equals(23));
      expect(threeReply.y, equals(42));
      expect(threeReply.z.foo, equals('hello'));
      expect(threeReply.z.bar, equals(1729));
      expect(threeReply.z.baz, unorderedEquals(primes));
    });

    test('one handle arg', () async {
      final pair = System.channelCreate();
      final handleStr = pair.first.toString();
      final h1 = HandleStruct(foo: 'hello', bar: 42, baz: pair.first);

      final oneHandleReply = await server.proxy.twoWayOneHandleArg(h1);

      expect(oneHandleReply.foo, equals('hello'));
      expect(oneHandleReply.bar, equals(42));
      expect(oneHandleReply.baz.toString(), equals(handleStr));
    });

    test('two handle args', () async {
      final pair = System.channelCreate();
      final firstHandleStr = pair.first.toString();
      final secondHandleStr = pair.second.toString();
      final h1 = HandleStruct(foo: 'hello', bar: 42, baz: pair.first);
      final h2 = HandleStruct(foo: 'goodbye', bar: 24, baz: pair.second);

      final twoHandleReply = await server.proxy.twoWayTwoHandleArgs(h1, h2);

      expect(twoHandleReply.h1.foo, equals('hello'));
      expect(twoHandleReply.h1.bar, equals(42));
      expect(twoHandleReply.h1.baz.toString(), equals(firstHandleStr));
      expect(twoHandleReply.h2.foo, equals('goodbye'));
      expect(twoHandleReply.h2.bar, equals(24));
      expect(twoHandleReply.h2.baz.toString(), equals(secondHandleStr));
    });
  });
}
