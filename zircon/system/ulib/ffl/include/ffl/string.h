// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef FFL_STRING_H_
#define FFL_STRING_H_

#include <ffl/fixed.h>
#include <ffl/saturating_arithmetic.h>

namespace ffl {

// String builds and stores a null-terminated string representation of a fixed-
// point value. A constant-size string buffer is maintained internally in each
// instance to permit temporaries in calls to printing/logging functions.
class String {
 public:
  constexpr String() = default;
  constexpr String(const String&) = default;
  constexpr String& operator=(const String&) = default;

  // Constructs a String containing a basic decimal string representation
  // of the given fixed-point value.
  template <typename Integer, size_t FractionalBits>
  constexpr String(Fixed<Integer, FractionalBits> value) {
    BasicFormat(value);
  }

  // Returns a pointer to the internal string. The string is guaranteed to be
  // null-terminated.
  constexpr const char* c_str() const { return buffer_; }

  // Returns a pointer to the first element of the internal string buffer.
  constexpr const char* data() const { return buffer_; }

 private:
  template <typename Integer, size_t FractionalBits>
  constexpr void BasicFormat(Fixed<Integer, FractionalBits> value);

  // Display fractional decimal digits down to 1e-10.
  static constexpr size_t kFractionalDigits = 10;

  // Allocate space for the integral digits, fractional digits, decimal point,
  // sign, and terminating null. The maximum number of chars produced by the
  // current format is actually 29.
  static constexpr size_t kBufferSize = 32;

  // Default-initialize the buffer to encourage constexpr evaluation.
  char buffer_[kBufferSize]{};
};

// Utility that returns a String for the given Fixed value. This function may
// be looked up by ADL to reduce namespace clutter at call sites. The noinline
// attribute avoids unnecessary expansion around logging/printing calls.
template <typename Integer, size_t FractionalBits>
[[gnu::noinline]] constexpr String Format(Fixed<Integer, FractionalBits> value) {
  return {value};
}

// Formats the given Fixed value into the string buffer in basic decimal format.
template <typename Integer, size_t FractionalBits>
constexpr void String::BasicFormat(Fixed<Integer, FractionalBits> value) {
  using Format = typename Fixed<Integer, FractionalBits>::Format;
  size_t start = 0;

  // Record the sign before converting the intermediate values to positive.
  if (value.raw_value() < 0) {
    buffer_[start++] = '-';
  }

  // Convert the intermediate values to positive for string conversion.
  const Integer mask = static_cast<Integer>(-(value.raw_value() < 0));
  const Integer one = mask & 1;
  const uint64_t absolute_value = SaturateAddAs<uint64_t>(value.raw_value() ^ mask, one);
  const uint64_t integral_value =
      Format::FractionalBits == 64 ? 0 : absolute_value >> Format::FractionalBits;

  // Reserve space in the buffer for the integral digits, including when the
  // integral component is zero.
  uint64_t remaining_value = integral_value;
  do {
    start++;
    remaining_value /= 10;
  } while (remaining_value > 0);

  // Mark the beginning of the fractional region and store decimal point.
  size_t end = start + 1;
  buffer_[start--] = '.';

  // String convert the integral component into the reserved region.
  remaining_value = integral_value;
  do {
    buffer_[start--] = static_cast<char>('0' + remaining_value % 10);
    remaining_value /= 10;
  } while (remaining_value > 0);

  // Up or down convert the fractional value to 60bits. The intermediate value
  // must have at least 4bits of headroom to compute each decimal digit.
  using F = Fixed<uint64_t, 60>;
  if constexpr (Format::FractionalBits >= F::Format::FractionalBits) {
    remaining_value = absolute_value >> (Format::FractionalBits - F::Format::FractionalBits);
  } else {
    remaining_value = absolute_value << (F::Format::FractionalBits - Format::FractionalBits);
  }

  // String convert the fractional component into the remaining buffer,
  // keeping track of the last non-zero trailing digit.
  size_t last_nonzero = end;
  const size_t stop = end + kFractionalDigits;
  do {
    remaining_value &= F::Format::FractionalMask;
    remaining_value *= 10;
    const char digit = static_cast<char>('0' + (remaining_value >> F::Format::FractionalBits));
    if (digit != '0') {
      last_nonzero = end;
    }
    buffer_[end++] = digit;
  } while (remaining_value != 0 && end < stop);

  // Set the null termination after the last non-zero trailing digit.
  buffer_[last_nonzero + 1] = '\0';
}

}  // namespace ffl

#endif  // FFL_STRING_H_
