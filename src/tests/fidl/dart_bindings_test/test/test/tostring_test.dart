// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';

void main() {
  print('toString-test');
  group('bits', () {
    test('no bit', () {
      expect(ExampleBits.$none.toString(), equals(r'ExampleBits.$none'));
    });
    test('single bit', () {
      expect(ExampleBits.memberC.toString(), equals(r'ExampleBits.memberC'));
    });
    test('multiple bits', () {
      expect((ExampleBits.memberC | ExampleBits.memberA).toString(),
          equals(r'ExampleBits.memberA | ExampleBits.memberC'));
    });
    test('unknown bits', () {
      expect((FlexibleBits.one | FlexibleBits(4)).toString(),
          equals(r'FlexibleBits.one | 0x4'));
    });
  });
  group('enum', () {
    test('strict-enum', () {
      expect(EnumTwo.two.toString(), equals(r'EnumTwo.two'));
    });
    test('flexible-enum', () {
      expect(
          FlexibleEnumThree.two.toString(), equals(r'FlexibleEnumThree.two'));
      expect(FlexibleEnumThree(5).toString(), equals(r'FlexibleEnumThree.5'));
    });
  });
}
