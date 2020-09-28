// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/49485) - this is a copy of bitfield64 from fuchsia_inspect. Find a
// way to share the common functionality.

/// BitRange specifies the [start] and [end] position values for [Bitfield64].
class BitRange {
  /// Least significant bit to be accessed.
  final int start;

  /// Most significant bit to be accessed.
  final int end;

  /// Range-sized bitmask starting at bit 0.
  final int maskAt0;

  /// Creates and sets the BitRange.
  ///
  /// Throws if [start] < 0, [end] > 63, or [start] > [end].
  ///
  /// The mask computation relies on the fact that 1 << 64 == 0 in Dart, and
  /// 0 - 1 == 0xffffffffffffffff in 2's complement arithmetic.
  BitRange(this.start, this.end) : maskAt0 = ((1 << (end - start + 1)) - 1) {
    if (end < start) {
      throw ArgumentError('End must be >= start.');
    }
    if (start < 0) {
      throw ArgumentError("Starting bit can't be less than zero.");
    }
    if (end > 63) {
      throw ArgumentError('There are only 64 bits.');
    }
  }
}

/// Bitfield64 stores a 64-bit value, and reads and writes bitfields on it.
class Bitfield64 {
  /// The full 64-bit value. It acts like a little endian number.
  int value = 0;

  /// Creates a bitfield initialized to a value.
  Bitfield64([this.value = 0]);

  /// Returns value of bits.
  ///
  /// For example, <value of next-to-smallest byte> = read(BitRange(8, 15)).
  // When reading, shift first and then mask; masking and then
  // shifting when bit 63 == 1 makes the return value negative.
  int read(BitRange range) => (value >> range.start) & range.maskAt0;

  /// Writes the lowest bits of [value] to the indicated range.
  void write(BitRange range, int value) {
    this.value &= ~(range.maskAt0 << range.start);
    this.value |= (value & range.maskAt0) << range.start;
  }
}
