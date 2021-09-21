// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:test/test.dart';

void main() {
  print('enum-test');
  group('isUnknown', () {
    test('strict-enum', () {
      expect(EnumOne.one.isUnknown(), isFalse);
    });
    test('flexible-enum-known', () {
      expect(FlexibleEnumThree.one.isUnknown(), isFalse);
    });
    test('flexible-enum-unknown', () {
      expect(FlexibleEnumThree(5).isUnknown(), isTrue);
      expect(FlexibleEnumThree.$unknown.isUnknown(), isTrue);
      expect(FlexibleEnumFour.$unknown.isUnknown(), isTrue);
      expect(FlexibleEnumFour.customUnknown.isUnknown(), isTrue);
    });
  });
  group('unknown placeholders', () {
    test(r'$unknown', () {
      expect(FlexibleEnumThree.$unknown.$value, equals(0xFFFFFFFFFFFFFFFF));
      expect(FlexibleEnumFour.$unknown.$value, equals(-123));
    });
    test('custom unknown', () {
      expect(FlexibleEnumFour.customUnknown.$value, equals(-123));
    });
  });
}
