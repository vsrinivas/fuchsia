// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

// TODO(fxbug.dev/49485) - this is a copy of bitfield64 from fuchsia_inspect. Find a
// way to share the common functionality.

import 'package:fuchsia_diagnostic_streams/src/internal/bitfield64.dart';
import 'package:test/test.dart';

void main() {
  group('bit range exceptions', () {
    test('read start < 0', () {
      var bits = Bitfield64();
      expect(() => bits.read(BitRange(-1, 1)),
          throwsA(const TypeMatcher<ArgumentError>()));
    });

    test('read start > end', () {
      var bits = Bitfield64();
      expect(() => bits.read(BitRange(2, 1)),
          throwsA(const TypeMatcher<ArgumentError>()));
    });

    test('read end > 63', () {
      var bits = Bitfield64();
      expect(() => bits.read(BitRange(32, 64)),
          throwsA(const TypeMatcher<ArgumentError>()));
    });

    test('write start < 0', () {
      var bits = Bitfield64();
      expect(() => bits.write(BitRange(-1, 1), 1),
          throwsA(const TypeMatcher<ArgumentError>()));
    });

    test('write start > end', () {
      var bits = Bitfield64();
      expect(() => bits.write(BitRange(2, 1), 1),
          throwsA(const TypeMatcher<ArgumentError>()));
    });

    test('write end > 63', () {
      var bits = Bitfield64();
      expect(() => bits.write(BitRange(32, 64), 1),
          throwsA(const TypeMatcher<ArgumentError>()));
    });
  });

  group('full 64 bit', () {
    // This test should fail if we somehow end up on a 53-bit-int Javascript
    // platform.
    test('stores big precise values', () {
      var bits = Bitfield64(0x1020304050607080);
      expect(bits.read(BitRange(0, 63)), 0x1020304050607080);
      bits.write(BitRange(0, 4), 1);
      expect(bits.read(BitRange(0, 63)), 0x1020304050607081);
      bits.write(BitRange(0, 4), 2);
      expect(bits.read(BitRange(0, 63)), 0x1020304050607082);
      bits.write(BitRange(0, 4), 3);
      expect(bits.read(BitRange(0, 63)), 0x1020304050607083);
    });
  });

  group('field manipulations', () {
    test('can set low bit', () {
      var bits = Bitfield64()..write(BitRange(0, 0), 1);
      expect(bits.read(BitRange(0, 63)), 1);
    });

    test('can set high bit', () {
      var bits = Bitfield64()..write(BitRange(63, 63), 1);
      expect(bits.read(BitRange(0, 63)), 0x8000000000000000);
    });

    test("adjacent fields don't interfere", () {
      var bits = Bitfield64()
        ..write(BitRange(0, 11), 0xaaa)
        ..write(BitRange(4, 7), 0x5);
      expect(bits.read(BitRange(0, 11)), 0xa5a);
    });

    test('proper truncation in and out', () {
      var bits = Bitfield64()..write(BitRange(0, 11), 0xafff);
      expect(bits.read(BitRange(0, 15)), 0xfff);
      expect(bits.read(BitRange(2, 9)), 0xff);
    });

    test("write doesn't include previous bit values", () {
      var bits = Bitfield64(0xffff);
      expect(bits.read(BitRange(0, 15)), 0xffff);
      bits.write(BitRange(4, 11), 0);
      expect(bits.read(BitRange(0, 15)), 0xf00f);
      bits.write(BitRange(5, 10), 0xffff);
      expect(bits.read(BitRange(0, 16)), 0xf7ef);
    });

    test('bit 63 1 -> positive value on read', () {
      var bits = Bitfield64()..write(BitRange(60, 63), 0xd);
      expect(bits.read(BitRange(60, 63)), 0xd);
    });
  });
}
