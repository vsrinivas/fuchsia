// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:fidl/fidl.dart' show UnknownRawData;
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart' show Handle, System;

void main() {
  group('hash and equality', () {
    group('enums', () {
      test('simple', () {
        expect(EnumOne.one == EnumOne.one, isTrue);
        expect(EnumOne.one == EnumOne.two, isFalse);
      });
      test('constructed', () {
        expect(EnumOne.one == EnumOne(1), isTrue);
      });
      test('different types', () {
        expect(EnumOne.one == EnumTwo.one, isFalse);
      });
      test('map', () {
        final Map<EnumOne, String> map = {EnumOne.three: 'tres'};
        expect(map[EnumOne.three], equals('tres'));
      });
    });

    group('structs', () {
      test('no handles', () {
        final s1 = NoHandleStruct(
            foo: 'hello', bar: 42, baz: Uint8List.fromList([2, 4, 6, 8]));
        final s2 = NoHandleStruct(
            foo: 'hello', bar: 42, baz: Uint8List.fromList([2, 4, 6, 8]));
        final s3 = NoHandleStruct(
            foo: 'goodbye', bar: 42, baz: Uint8List.fromList([2, 4, 6, 8]));
        final s4 = NoHandleStruct(
            foo: 'hello', bar: 42, baz: Uint8List.fromList([3, 6, 9, 12]));
        expect(s1 == s2, isTrue);
        expect(s1 == s3, isFalse);
        expect(s1 == s4, isFalse);

        final Map<NoHandleStruct, String> map = {s1: 'yes'};
        expect(map[s2], equals('yes'));
        expect(map[s3], equals(null));
        expect(map[s4], equals(null));
      });

      test('handles', () {
        final i1 = HandleStruct(foo: 'hello', bar: 42, baz: Handle.invalid());
        final i2 = HandleStruct(foo: 'hello', bar: 42, baz: Handle.invalid());
        expect(i1 == i2, isTrue);

        final pair = System.channelCreate();
        final f1 = HandleStruct(foo: 'hello', bar: 42, baz: pair.first);
        final f2 = HandleStruct(foo: 'hello', bar: 42, baz: pair.first);
        final s = HandleStruct(foo: 'hello', bar: 42, baz: pair.second);
        expect(f1 == f2, isTrue);
        expect(f1 == s, isFalse);
        expect(i1 == f1, isFalse);

        final Map<HandleStruct, String> map = {
          i1: 'invalid',
          f1: 'first',
          s: 'second',
        };
        expect(map[i2], equals('invalid'));
        expect(map[f2], equals('first'));
        expect(map[s], equals('second'));
        pair.first.close();
        pair.second.close();
      });
    });

    group('unions', () {
      test('equality', () {
        expect(UnionOne.withFoo('hello') == UnionOne.withFoo('hello'), isTrue);
        expect(UnionOne.withFoo('hello') == UnionOne.withFoo('ciao'), isFalse);
        expect(UnionOne.withFoo('hello') == UnionOne.withBar('hello'), isFalse);
        expect(UnionOne.withFoo('hello') == UnionTwo.withFoo('hello'), isFalse);
      });
      test('hash', () {
        final Map<UnionOne, String> map = {
          UnionOne.withFoo('hello'): 'hello foo',
          UnionOne.withBar('hello'): 'hello bar',
          UnionOne.withFoo('hola'): 'hola foo',
          UnionOne.withBar('hola'): 'hola bar',
        };
        expect(map[UnionOne.withFoo('hello')], equals('hello foo'));
        expect(map[UnionOne.withBar('hello')], equals('hello bar'));
        expect(map[UnionOne.withFoo('hola')], equals('hola foo'));
        expect(map[UnionOne.withBar('hola')], equals('hola bar'));
      });
    });

    group('unknown raw data', () {
      test('equality', () {
        expect(
            UnknownRawData(Uint8List.fromList([1, 2, 3]), [Handle.invalid()]) ==
                UnknownRawData(
                    Uint8List.fromList([1, 2, 3]), [Handle.invalid()]),
            isTrue);
        expect(
            UnknownRawData(Uint8List.fromList([1, 2, 3]), []) ==
                UnknownRawData(
                    Uint8List.fromList([1, 2, 3]), [Handle.invalid()]),
            isFalse);
        expect(
            UnknownRawData(Uint8List.fromList([2, 1, 3]), [Handle.invalid()]) ==
                UnknownRawData(
                    Uint8List.fromList([1, 2, 3]), [Handle.invalid()]),
            isFalse);
        expect(
            UnknownRawData(Uint8List(5), []) ==
                UnknownRawData(Uint8List(5), []),
            isTrue);
        expect(
            UnknownRawData(Uint8List(2), []) ==
                UnknownRawData(Uint8List(3), []),
            isFalse);
      });

      test('hash', () {
        final Map<UnknownRawData, String> map = {
          UnknownRawData(Uint8List(0), []): 'empty',
          UnknownRawData(Uint8List.fromList([1, 2, 3]), []): 'with bytes',
          UnknownRawData(Uint8List.fromList([2, 2, 3]), []): 'different bytes',
          UnknownRawData(Uint8List.fromList([1, 2, 3]), [Handle.invalid()]):
              'with handle',
        };
        expect(map[UnknownRawData(Uint8List(0), [])], equals('empty'));
        expect(map[UnknownRawData(Uint8List.fromList([1, 2, 3]), [])],
            equals('with bytes'));
        expect(map[UnknownRawData(Uint8List.fromList([2, 2, 3]), [])],
            equals('different bytes'));
        expect(
            map[UnknownRawData(
                Uint8List.fromList([1, 2, 3]), [Handle.invalid()])],
            equals('with handle'));
      });
    });
  });
}
