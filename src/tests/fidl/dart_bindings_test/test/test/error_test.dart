// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:fidl/fidl.dart' show MethodException;
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';

import './server.dart';

void main() async {
  TestServerInstance server;

  group('error', () {
    setUpAll(() async {
      server = TestServerInstance();
      await server.start();
    });

    tearDownAll(() async {
      await server.stop();
      server = null;
    });

    test('zero arguments, int error', () {
      expect(server.proxy.replyWithErrorZero(false), completion(anything));
      expect(
          server.proxy.replyWithErrorZero(true),
          throwsA(
              predicate((err) => err is MethodException && err.value == 23)));
    });

    test('one argument, int error', () {
      expect(server.proxy.replyWithErrorOne(false, 'hello'),
          completion(equals('hello')));
      expect(
          server.proxy.replyWithErrorOne(true, 'hello'),
          throwsA(
              predicate((err) => err is MethodException && err.value == 42)));
    });

    test('more arguments, int error', () {
      expect(
          server.proxy.replyWithErrorMore(false, 'hello', false),
          completion(predicate(
              (TestServer$ReplyWithErrorMore$Response response) =>
                  response.value == 'hello' && response.otherValue == false)));
      expect(
          server.proxy.replyWithErrorMore(true, 'hello', false),
          throwsA(
              predicate((err) => err is MethodException && err.value == 666)));
    });

    test('zero arguments, enum error', () {
      expect(server.proxy.replyWithErrorEnumZero(false), completion(anything));
      expect(
          server.proxy.replyWithErrorEnumZero(true),
          throwsA(predicate(
              (err) => err is MethodException && err.value == EnumOne.one)));
    });

    test('one argument, enum error', () {
      expect(server.proxy.replyWithErrorEnumOne(false, 'hello'),
          completion(equals('hello')));
      expect(
          server.proxy.replyWithErrorEnumOne(true, 'hello'),
          throwsA(predicate(
              (err) => err is MethodException && err.value == EnumOne.two)));
    });

    test('more arguments, enum error', () {
      expect(
          server.proxy.replyWithErrorEnumMore(false, 'hello', false),
          completion(predicate(
              (TestServer$ReplyWithErrorEnumMore$Response response) =>
                  response.value == 'hello' && response.otherValue == false)));
      expect(
          server.proxy.replyWithErrorEnumMore(true, 'hello', false),
          throwsA(predicate(
              (err) => err is MethodException && err.value == EnumOne.three)));
    });
  });
}
