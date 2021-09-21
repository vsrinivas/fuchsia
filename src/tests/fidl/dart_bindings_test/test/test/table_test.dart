// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:test/test.dart';

void main() {
  group('tables', () {
    test('withUnknown', () {
      final data =
          UnknownRawData(Uint8List.fromList([1, 2, 3, 4, 5, 6, 7, 8]), []);
      final table = ExampleTable(bar: 1, $unknownData: {4: data});
      expect(table.foo, isNull);
      expect(table.bar, equals(1));
      expect(table.baz, isNull);
      expect(table.$unknownData, {4: data});
    });

    test('noUnknown', () {
      final table = ExampleTable(foo: 'foo', bar: 3);
      expect(table.foo, equals('foo'));
      expect(table.bar, equals(3));
      expect(table.baz, isNull);
      expect(table.$unknownData, isNull);
    });
  });
}
