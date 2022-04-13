// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:test/test.dart';

void main() {
  print('clone-test');
  group('clone and cloneWithout struct', () {
    test('exact', () {
      final s1 = ExampleStruct(foo: 'test', bar: 42);
      final s2 = ExampleStruct.clone(s1);
      expect(s2.foo, equals('test'));
      expect(s2.bar, equals(42));
      expect(s2.baz, equals(null));
    });
    test('modify field', () {
      final s1 = ExampleStruct(foo: 'test', bar: 42);
      final s2 = ExampleStruct.clone(s1, foo: 'hello');
      expect(s2.foo, equals('hello'));
      expect(s2.bar, equals(42));
      expect(s2.baz, equals(null));
    });
    test('set field', () {
      final s1 = ExampleStruct(foo: 'test', bar: 42);
      final s2 = ExampleStruct.clone(s1, baz: Uint8List(10));
      expect(s2.foo, equals('test'));
      expect(s2.bar, equals(42));
      expect(s2.baz?.length, equals(10));
    });
    test('unset field', () {
      final s1 = ExampleStruct(foo: 'test', bar: 42, baz: Uint8List(10));
      final s2 = ExampleStruct.cloneWithout(s1, baz: true);
      expect(s2.foo, equals('test'));
      expect(s2.bar, equals(42));
      expect(s2.baz, equals(null));
    });
  });

  group('struct cloneWith', () {
    test('exact', () {
      final s1 = ExampleStruct(foo: 'test', bar: 42);
      final s2 = s1.$cloneWith();
      expect(s2.foo, equals('test'));
      expect(s2.bar, equals(42));
      expect(s2.baz, equals(null));
    });
    test('modify field', () {
      final s1 = ExampleStruct(foo: 'test', bar: 42);
      final s2 = s1.$cloneWith(foo: 'hello');
      expect(s2.foo, equals('hello'));
      expect(s2.bar, equals(42));
      expect(s2.baz, equals(null));
    });
    test('set field', () {
      final s1 = ExampleStruct(foo: 'test', bar: 42);
      final s2 = s1.$cloneWith(baz: Some(Uint8List(10)));
      expect(s2.foo, equals('test'));
      expect(s2.bar, equals(42));
      expect(s2.baz?.length, equals(10));
    });
    test('unset field', () {
      final s1 = ExampleStruct(foo: 'test', bar: 42, baz: Uint8List(10));
      final s2 = s1.$cloneWith(baz: None());
      expect(s2.foo, equals('test'));
      expect(s2.bar, equals(42));
      expect(s2.baz, equals(null));
    });
  });

  group('table cloneWith()', () {
    test('exact', () {
      final t1 = ExampleTable(foo: 'foo', bar: 3);
      final t2 = t1.$cloneWith();
      expect(t2.foo, equals('foo'));
      expect(t2.bar, equals(3));
      expect(t2.baz, isNull);
      expect(t2.$unknownData, isNull);
    });
    test('modify field', () {
      final t1 = ExampleTable(foo: 'foo', bar: 3);
      final t2 = t1.$cloneWith(foo: Some('hello'));
      expect(t2.foo, equals('hello'));
      expect(t2.bar, equals(3));
      expect(t2.baz, isNull);
      expect(t2.$unknownData, isNull);
    });
    test('set field', () {
      final t1 = ExampleTable(foo: 'foo', bar: 3);
      final t2 = t1.$cloneWith(baz: Some(Uint8List(10)));
      expect(t2.foo, equals('foo'));
      expect(t2.bar, equals(3));
      expect(t2.baz?.length, equals(10));
      expect(t2.$unknownData, isNull);
    });
    test('unset field', () {
      final t1 = ExampleTable(foo: 'foo', bar: 3, baz: Uint8List(10));
      final t2 = t1.$cloneWith(baz: None());
      expect(t2.foo, equals('foo'));
      expect(t2.bar, equals(3));
      expect(t2.baz, isNull);
      expect(t2.$unknownData, isNull);
    });
  });
}
