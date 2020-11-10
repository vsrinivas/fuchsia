// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';

void main() {
  print('clone-test');
  group('clone', () {
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
}
