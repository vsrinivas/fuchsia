// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:test/test.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';

void main() {
  group('unions', () {
    test('withUnknown', () {
      final data =
          UnknownRawData(Uint8List.fromList([1, 2, 3, 4, 5, 6, 7, 8]), []);
      final union = ExampleXunion.with$UnknownData(17, data);
      expect(union.foo, isNull);
      expect(union.bar, isNull);
      expect(union.$tag, equals(ExampleXunionTag.$unknown));
      expect(union.$ordinal, equals(17));
      expect(union.$unknownData, equals(data));
    });

    test('noUnknown', () {
      final union = ExampleXunion.withFoo('foo');
      expect(union.foo, equals('foo'));
      expect(union.bar, isNull);
      expect(union.$tag, equals(ExampleXunionTag.foo));
      expect(union.$ordinal, equals(1));
      expect(union.$unknownData, isNull);
    });
  });
}
