// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'queries/index.dart';
import 'reflect.dart';
import 'types.dart';

// ignore_for_file: avoid_as

class Foo extends Query {
  @override
  void addReport(Report report) {}

  @override
  QueryReport distill() {
    throw UnimplementedError();
  }

  @override
  void mergeWith(Iterable<Query> others) {}

  @override
  String getDescription() {
    throw UnimplementedError();
  }
}

class FooOneIntArg extends Foo {
  FooOneIntArg({int a = 42}) : _a = a;
  final int _a;
}

class FooOneStringArg extends Foo {
  FooOneStringArg({String s = '42'}) : _s = s;
  final String _s;
}

class FooBoolStringArg extends Foo {
  FooBoolStringArg({bool b = false, String s = '42'})
      : _b = b,
        _s = s;
  final bool _b;
  final String _s;
}

void main() {
  group('ReflectQuery', () {
    test('Construct Foo()', () {
      final result = ReflectQuery.instantiate(
          const QueryFactory(Foo, ''), <String, String>{});
      expect(result, isA<Foo>());
    });

    test('Construct FooOneIntArg(a: 100)', () {
      final result = ReflectQuery.instantiate(
          const QueryFactory(FooOneIntArg, ''), <String, String>{'a': '100'});
      expect(result, isA<FooOneIntArg>());
      expect((result as FooOneIntArg)._a, equals(100));
    });

    test('Construct FooOneStringArg(s: 100)', () {
      final result = ReflectQuery.instantiate(
          const QueryFactory(FooOneStringArg, ''),
          <String, String>{'s': '100'});
      expect(result, isA<FooOneStringArg>());
      expect((result as FooOneStringArg)._s, equals('100'));
    });

    test('Construct FooBoolStringArg(b: false, s: 100)', () {
      final result = ReflectQuery.instantiate(
          const QueryFactory(FooBoolStringArg, ''),
          <String, String>{'b': 'true'});
      expect(result, isA<FooBoolStringArg>());
      expect((result as FooBoolStringArg)._b, equals(true));
      expect((result)._s, equals('42'));
    });
  });
}
