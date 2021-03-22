// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef FFL_STRING_H_
#define FFL_STRING_H_

#ifndef _KERNEL
#include <iomanip>
#include <iostream>
#endif

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

  enum Mode {
    // The value is rendered as an ordinary decimal number. The fraction is
    // limited to max_fractional_digits (which is capped internally so that
    // the resulting string is never longer than 31 characters).
    Dec,

    // The value is rendered as two unsigned hexidecimal numbers separated by a
    // decimal point, with FractionalBits after the decimal point. For example,
    // Fixed<int8_t,2>::FromRaw(0x0f) is rendered as "3.C".
    Hex,

    // The value is rendered as an rational expression of the form:
    //
    //    [optional sign][integer][sign][numerator]/[denominator]
    //
    // Each number is rendered in base 10. The fraction is not reduced, meaning
    // the denominator is always 2^FractionalBits. Examples:
    //
    //    Fixed<int8_t,2>::FromRaw(0x0f) => "3+3/4"
    //    Fixed<int8_t,2>::FromRaw(0xee) => "-4-2/4"
    //
    DecRational,
  };

  // Constructs a String containing a string representation of the given fixed-
  // point value. See Mode for a description of the available modes.
  template <typename Integer, size_t FractionalBits>
  constexpr String(Fixed<Integer, FractionalBits> value, Mode mode = Dec,
                   size_t max_fractional_digits = 10) {
    if (mode == Dec) {
      WriteDec(value, max_fractional_digits);
    } else if (mode == Hex) {
      WriteHex(value);
    } else if (mode == DecRational) {
      WriteDecRational(value);
    } else {
      __builtin_abort();
    }
  }

  // Returns a pointer to the internal string. The string is guaranteed to be
  // null-terminated.
  constexpr const char* c_str() const { return buffer_; }

  // Returns a pointer to the first element of the internal string buffer.
  constexpr const char* data() const { return buffer_; }

  // Returns the length of the string.
  constexpr size_t size() const { return length_; }

 private:
  template <typename Integer, size_t FractionalBits>
  constexpr void WriteDec(Fixed<Integer, FractionalBits> value, size_t max_fractional_digits);

  template <typename Integer, size_t FractionalBits>
  constexpr void WriteDecRational(Fixed<Integer, FractionalBits> value);

  constexpr void WriteDecInteger(uint64_t value);

  template <typename Integer, size_t FractionalBits>
  constexpr void WriteHex(Fixed<Integer, FractionalBits> value);

  enum ZeroMode {
    NoLeadingZeros,
    NoTrailingZeros,
  };
  constexpr void WriteHexInteger(uint64_t value, size_t digits, ZeroMode zero_mode);

  // For mode=Dec, we may need an arbitrary number of digits to format the
  // number with full precision. However, in order to format the number with
  // enough precision such that it can be reconstructed into the same exact
  // fixed-point value, the maximum buffer size needed is:
  //
  //   kBufferSize
  //       = ceil(log10(2^IntegralBits))
  //       + ceil(log10(2^FractionalBits))
  //       + 1  /* sign */
  //       + 1  /* decimal point */
  //       + 1  /* trailing '\0' */
  //       = 24
  //
  // For mode=Hex, the maximum buffer size needed is:
  //
  //   kBufferSize
  //       = log16(sizeof(uint64)*8)
  //       + 1  /* decimal point */
  //       + 1  /* trailing '\0' */
  //       + 2  /* '0x' prefix, for std::showbase */
  //       = 17
  //
  // For mode=DecRational, the maximum buffer size needed is:
  //
  //   kBufferSize
  //       = ceil(log10(2^IntegralBits))
  //       + ceil(log10(2^FractionalBits))
  //       + ceil(log10(2^FractionalBits))
  //       + 1  /* sign before integer */
  //       + 1  /* sign before fraction */
  //       + 1  /* slash */
  //       + 1  /* trailing '\0' */
  //       = 43
  //
  // We round sizeof(String) up to the nearest multiple of 8.
  static constexpr size_t kBufferSize = 47;

  // Default-initialize the buffer to encourage constexpr evaluation.
  char buffer_[kBufferSize]{};

  // String length.
  uint8_t length_ = 0;
};

// Utility that returns a String for the given Fixed value. This function may
// be looked up by ADL to reduce namespace clutter at call sites. The noinline
// attribute avoids unnecessary expansion around logging/printing calls.
template <typename Integer, size_t FractionalBits>
[[gnu::noinline]] constexpr String Format(Fixed<Integer, FractionalBits> value,
                                          String::Mode mode = String::Dec,
                                          size_t max_fractional_digits = 10) {
  return {value, mode, max_fractional_digits};
}

// Renders the given fixed-point value in decimal
// Output starts at buffer[length_].
template <typename Integer, size_t FractionalBits>
constexpr void String::WriteDec(Fixed<Integer, FractionalBits> value,
                                size_t max_fractional_digits) {
  using Format = typename Fixed<Integer, FractionalBits>::Format;

  // Record the sign before converting the intermediate values to positive.
  if (value.raw_value() < 0) {
    buffer_[length_++] = '-';
  }

  // Below, we upcast to __uint128_t so we can elide overflow checking.
  static_assert(sizeof(Integer) * 8 <= 64);

  // Convert the intermediate values to positive for string conversion.
  const Integer mask = static_cast<Integer>(-(value.raw_value() < 0));
  const uint64_t one = static_cast<uint64_t>(mask & 1);
  // This operation cannot overflow:
  // When value.raw_value() >= 0, then mask ==  0 and one == 0
  // When value.raw_value()  < 0, then mask == ~0 and one == 1
  //                              and  value.raw_value() ^ mask < 2^63
  const uint64_t absolute_value = static_cast<uint64_t>(value.raw_value() ^ mask) + one;
  const uint64_t integral_value =
      Format::FractionalBits == 64 ? 0 : absolute_value >> Format::FractionalBits;

  // Write the integral part.
  WriteDecInteger(integral_value);

  // Write the fractional part.
  if (max_fractional_digits > 0) {
    buffer_[length_] = '.';

    size_t pos = length_ + 1;
    size_t last_nonzero = pos;

    // Stop when requested or when the buffer ends, whichever comes first.
    const size_t requested_stop = pos + max_fractional_digits;
    const size_t stop = requested_stop < (kBufferSize - 1) ? requested_stop : (kBufferSize - 1);

    __uint128_t remaining_value = absolute_value;
    do {
      remaining_value &= Format::FractionalMask;
      remaining_value *= 10;
      const char digit = static_cast<char>('0' + (remaining_value >> Format::FractionalBits));
      if (digit != '0') {
        last_nonzero = pos;
      }
      buffer_[pos++] = digit;
    } while (remaining_value != 0 && pos < stop);

    // End the string after the last non-zero trailing digit.
    length_ = static_cast<uint8_t>(last_nonzero + 1);
  }

  buffer_[length_] = '\0';
}

// Renders the given fixed-point value as a decimal fraction.
// Output starts at buffer[length_].
template <typename Integer, size_t FractionalBits>
constexpr void String::WriteDecRational(Fixed<Integer, FractionalBits> value) {
  using Format = typename Fixed<Integer, FractionalBits>::Format;

  // Record the sign before converting the intermediate values to positive.
  const char sign = (value.raw_value() < 0) ? '-' : '+';
  if (value.raw_value() < 0) {
    buffer_[length_++] = '-';
  }

  // Convert the intermediate values to positive for string conversion.
  const Integer mask = static_cast<Integer>(-(value.raw_value() < 0));
  const uint64_t one = static_cast<uint64_t>(mask & 1);
  // This operation cannot overflow:
  // When value.raw_value() >= 0, then mask ==  0 and one == 0
  // When value.raw_value()  < 0, then mask == ~0 and one == 1
  //                              and  value.raw_value() ^ mask < 2^63
  const uint64_t absolute_value = static_cast<uint64_t>(value.raw_value() ^ mask) + one;
  const uint64_t integral_value =
      Format::FractionalBits == 64 ? 0 : absolute_value >> Format::FractionalBits;

  // Write the integral part.
  WriteDecInteger(integral_value);

  // Write the fractional part.
  buffer_[length_++] = sign;
  WriteDecInteger(absolute_value & Format::FractionalMask);
  buffer_[length_++] = '/';

  // Write out 2^64 manually, since that doesn't fit in a 64-bit number.
  if (Format::FractionalBits == 64) {
    const char str[] = "18446744073709551616";
    memcpy(&buffer_[length_], "18446744073709551616", sizeof(str));
    length_ += sizeof(str) - 1;
  } else {
    WriteDecInteger(Format::Power);
    buffer_[length_] = '\0';
  }
}

// Renders the given integer as a decimal number.
// Output starts at buffer[length_].
constexpr void String::WriteDecInteger(uint64_t value) {
  // Reserve space in the buffer for the integral digits, including when the
  // integral component is zero.
  uint64_t remaining_value = value;
  do {
    length_++;
    remaining_value /= 10;
  } while (remaining_value > 0);

  // String convert the integral component into the reserved region.
  size_t pos = length_ - 1;
  remaining_value = value;
  do {
    buffer_[pos--] = static_cast<char>('0' + remaining_value % 10);
    remaining_value /= 10;
  } while (remaining_value > 0);
}

// Renders the given fixed-point value as a hexadecimal fraction.
// Output starts at buffer[length_].
template <typename Integer, size_t FractionalBits>
constexpr void String::WriteHex(Fixed<Integer, FractionalBits> value) {
  using Format = typename Fixed<Integer, FractionalBits>::Format;

  // Cast the raw_value to a uint64_t without sign extension.
  const uint64_t raw_value_mask =
      (Format::Bits == 64) ? uint64_t(-1) : (uint64_t(1) << Format::Bits) - 1;
  const uint64_t raw_value = static_cast<uint64_t>(value.raw_value()) & raw_value_mask;

  // Integral portion.
  if constexpr (Format::FractionalBits == Format::Bits) {
    buffer_[length_++] = '0';
  } else {
    // Integral digits includes everything not covered by FractionalBits.
    // Shift the first integral hex digit into the MSB.
    const uint64_t integral_value = (raw_value & Format::IntegralMask) >> FractionalBits;
    const uint64_t integral_hex_digits = (Format::Bits - FractionalBits + 3) / 4;
    const uint64_t integral_shifted = integral_value << ((16 - integral_hex_digits) * 4);
    WriteHexInteger(integral_shifted, integral_hex_digits, NoLeadingZeros);
  }

  // Fractional portion.
  buffer_[length_++] = '.';
  if constexpr (Format::FractionalBits == 0) {
    buffer_[length_++] = '0';
  } else {
    // Shift the first fractional bit into the MSB.
    const uint64_t fractional_value = raw_value & Format::FractionalMask;
    const uint64_t fractional_shifted = fractional_value << (64 - Format::FractionalBits);
    const uint64_t fractional_hex_digits = (FractionalBits + 3) / 4;
    WriteHexInteger(fractional_shifted, fractional_hex_digits, NoTrailingZeros);
  }

  buffer_[length_] = '\0';
}

// Renders the given integer as a hexadecimal number.
// Output starts at buffer[length_].
constexpr void String::WriteHexInteger(uint64_t value, size_t digits, ZeroMode zero_mode) {
  if (!value) {
    buffer_[length_++] = '0';
    return;
  }

  bool had_nonzero = false;
  uint8_t last_nonzero = length_;

  for (size_t k = 0; k < digits; k++, value <<= 4) {
    auto digit = value >> 60;
    if (digit == 0) {
      if (zero_mode == NoLeadingZeros && !had_nonzero) {
        continue;
      }
    } else {
      had_nonzero = true;
      last_nonzero = length_;
    }
    if (digit < 10) {
      buffer_[length_++] = static_cast<char>('0' + digit);
    } else {
      buffer_[length_++] = static_cast<char>('a' + (digit - 10));
    }
  }

  if (zero_mode == NoTrailingZeros) {
    length_ = last_nonzero + 1;
  }
}

#ifndef _KERNEL

namespace internal {
extern const int kIosModeIndex;
}

// A stream manipulator for setting the current mode.
// Defaults to String::Dec.
std::ostream& operator<<(std::ostream& out, String::Mode mode);

// Operator to write a Fixed value into an ostream.
template <typename Integer, size_t FractionalBits>
std::ostream& operator<<(std::ostream& out, Fixed<Integer, FractionalBits> value) {
  auto mode = static_cast<String::Mode>(out.iword(internal::kIosModeIndex));
  if (mode == String::Hex && (out.flags() & std::ios_base::showbase)) {
    out << "0x";
  }
  out << String(value, mode, out.precision()).c_str();
  return out;
}

#endif

}  // namespace ffl

#endif  // FFL_STRING_H_
