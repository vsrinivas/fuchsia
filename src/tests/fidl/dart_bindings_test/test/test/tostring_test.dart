// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:test/test.dart';

void main() {
  print('toString-test');
  group('bits', () {
    test('no bit', () {
      expect(ExampleBits.$none.toString(), equals(r'ExampleBits(0)'));
    });
    test('single bit', () {
      expect(ExampleBits.memberC.toString(), equals(r'ExampleBits(8)'));
    });
    test('multiple bits', () {
      expect((ExampleBits.memberC | ExampleBits.memberA).toString(),
          equals(r'ExampleBits(10)'));
    });
    test('unknown bits', () {
      expect((FlexibleBits.one | FlexibleBits(4)).toString(),
          equals(r'FlexibleBits(5)'));
    });
  });
  group('enum', () {
    test('strict-enum', () {
      expect(EnumTwo.two.toString(), equals(r'EnumTwo(2)'));
    });
    test('flexible-enum', () {
      expect(FlexibleEnumThree.two.toString(), equals(r'FlexibleEnumThree(2)'));
      expect(FlexibleEnumThree(5).toString(), equals(r'FlexibleEnumThree(5)'));
    });
  });
}
