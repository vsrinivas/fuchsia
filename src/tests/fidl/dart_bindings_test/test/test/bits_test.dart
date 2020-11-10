// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';

void main() {
  group('unknown-bits', () {
    test('strict', () {
      expect(ExampleBits.memberA.hasUnknownBits(), isFalse);
      expect(ExampleBits.memberA.getUnknownBits(), equals(0));
    });
    test('flexible-known', () {
      expect(FlexibleBits.one.hasUnknownBits(), isFalse);
      expect(FlexibleBits.one.getUnknownBits(), equals(0));
    });
    test('flexible-unknown', () {
      expect(FlexibleBits(4).hasUnknownBits(), isTrue);
      expect(FlexibleBits(4).getUnknownBits(), equals(4));

      expect(FlexibleBits(15).hasUnknownBits(), isTrue);
      expect(FlexibleBits(15).getUnknownBits(), equals(12));
    });
  });

  group('bits-negation', () {
    test('strict', () {
      expect(~ExampleBits.memberA,
          equals(ExampleBits.memberB | ExampleBits.memberC));
      expect(~ExampleBits.$none, equals(ExampleBits.$mask));
    });

    test('flexible', () {
      expect(~FlexibleBits.one, equals(FlexibleBits.two));
      expect(~(FlexibleBits.one | FlexibleBits(4)), equals(FlexibleBits.two));
      expect(~FlexibleBits.$none, equals(FlexibleBits.$mask));
    });
  });
}
