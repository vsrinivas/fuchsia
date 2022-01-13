// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <optional>
#include <string_view>

#include <pretty/cpp/sizes.h>
#include <pretty/sizes.h>

namespace pretty {
namespace {

struct EncodedSize {
  // All numbers before the first '.'.
  std::string_view integral;

  // All numbers after the first '.'.
  std::string_view fractional;

  SizeUnit unit;

  uint64_t scale = 1;
};

std::optional<EncodedSize> ProcessFormattedString(std::string_view formatted) {
  if (formatted.empty()) {
    return std::nullopt;
  }

  EncodedSize encoded;
  encoded.unit = SizeUnit::kBytes;
  if (!isdigit(formatted.back())) {
    encoded.unit = static_cast<SizeUnit>(toupper(formatted.back()));
    formatted.remove_suffix(1);

    // Look for the unit.
    switch (encoded.unit) {
      case SizeUnit::kBytes:
        encoded.scale = 1;
        break;
      case SizeUnit::kKiB:
        encoded.scale <<= 10;
        break;
      case SizeUnit::kMiB:
        encoded.scale <<= 20;
        break;
      case SizeUnit::kGiB:
        encoded.scale <<= 30;
        break;
      case SizeUnit::kTiB:
        encoded.scale <<= 40;
        break;
      case SizeUnit::kPiB:
        encoded.scale <<= 50;
        break;
      case SizeUnit::kEiB:
        encoded.scale <<= 60;
        break;
      default:
        return std::nullopt;
    }
  }

  size_t split_at = formatted.find('.');
  encoded.integral = formatted;

  // Adjust substrings for presence of decimal values.
  if (split_at != std::string_view::npos) {
    encoded.integral = formatted.substr(0, split_at);
    if (add_overflow(split_at, 1, &split_at) || split_at == formatted.length()) {
      return std::nullopt;
    }
    encoded.fractional = formatted.substr(split_at, formatted.length());
    // "A.[Unit]" with A being digit is still invalid.
    if (encoded.fractional.empty()) {
      return std::nullopt;
    }
  }

  if (encoded.integral.empty()) {
    return std::nullopt;
  }

  return encoded;
}

}  // namespace

std::string_view FormattedBytes::ToString(SizeUnit unit) {
  using namespace std::string_view_literals;

  switch (unit) {
    case SizeUnit::kAuto:
      break;
    case SizeUnit::kBytes:
      return "B"sv;
    case SizeUnit::kKiB:
      return "K"sv;
    case SizeUnit::kMiB:
      return "M"sv;
    case SizeUnit::kGiB:
      return "G"sv;
    case SizeUnit::kTiB:
      return "T"sv;
    case SizeUnit::kPiB:
      return "P"sv;
    case SizeUnit::kEiB:
      return "E"sv;
  }
  return {};
}

std::optional<uint64_t> ParseSizeBytes(std::string_view formatted_bytes) {
  auto encoded_size = ProcessFormattedString(formatted_bytes);
  if (!encoded_size) {
    return std::nullopt;
  }

  uint64_t integral = 0;
  uint64_t base_10 = 1;
  for (auto it = encoded_size->integral.rbegin(); it != encoded_size->integral.rend(); ++it) {
    char digit = *it;
    if (!isdigit(digit)) {
      return std::nullopt;
    }
    unsigned val = digit - '0';
    uint64_t scaled_val = val * base_10 * encoded_size->scale;

    if (add_overflow(integral, scaled_val, &integral)) {
      return std::nullopt;
    }
    base_10 *= 10;
  }
  base_10 = 1;

  uint64_t carry = 0;
  // This loop provides software division, because for the larger
  // units its is quite possible to overflow when doing the scaling
  // of the mantissa.
  // If one were to use the naive approach:
  //  * let m be the mantissa as an integer.
  //  * k the length of the mantissa.
  //  * u the scaling factor of the provided unit.
  //
  // The number of bytes encoded in the mantissa, can be calculated as:
  //     |m * u / 10^(k)|
  // The problem arises when |m| * |u| exceeds the capacity of 64 bits.
  uint64_t fractional = 0;
  for (char digit : encoded_size->fractional) {
    if (!isdigit(digit)) {
      return std::nullopt;
    }
    uint64_t val = digit - '0';
    base_10 *= 10;
    uint64_t scaled_value = val * encoded_size->scale;
    // Calculate how many bytes does this digit of the mantissa contributes.
    if (add_overflow(fractional, scaled_value / base_10, &fractional)) {
      return std::nullopt;
    }

    // Bring the carry from 10^-(i - 1) bytes to 10^-(i) bytes.
    carry = 10 * carry + scaled_value % base_10;

    // Try to consume any part of the accumulated carry.
    uint64_t consumed_carry = carry / base_10;
    if (add_overflow(fractional, consumed_carry, &fractional)) {
      return std::nullopt;
    }

    // Adjust the units back again.
    carry %= base_10;
  }

  // At this point there should be no carry left, unless we were given
  // a value that is not byte aligned (Y.X bytes) where X is non zero,
  // after applying the proper scaling.
  if (carry != 0) {
    return std::nullopt;
  }

  return integral + fractional;
}

}  // namespace pretty

// Calculate "n / d" as an integer, rounding any fractional part.
//
// The often-used expression "(n + (d / 2)) / d" can't be used due to
// potential overflow.
static size_t rounding_divide(size_t n, size_t d) {
  // If `n` is half way to the next multiple of `d`, we want to round up.
  // Otherwise, we truncate.
  bool round_up = ((n % d) >= (d / 2));

  return n / d + (round_up ? 1 : 0);
}

char* format_size_fixed(char* str, size_t str_size, size_t bytes, char unit) {
  static const char units[] = "BKMGTPE";
  static int num_units = sizeof(units) - 1;

  if (str_size == 0) {
    // Even if NULL.
    return str;
  }
  ZX_DEBUG_ASSERT(str != NULL);
  if (str_size == 1) {
    str[0] = '\0';
    return str;
  }

  char* orig_str = str;
  size_t orig_bytes = bytes;
retry:;
  int ui = 0;
  size_t divisor = 1;
  // If we have a fixed (non-zero) unit, divide until we hit it.
  //
  // Otherwise, divide until we reach a unit that can express the value
  // with 4 or fewer whole digits.
  // - If we can express the value without a fraction (it's a whole
  //   kibi/mebi/gibibyte), use the largest possible unit (e.g., favor
  //   "1M" over "1024k").
  // - Otherwise, favor more whole digits to retain precision (e.g.,
  //   favor "1025k" or "1025.0k" over "1.0M").
  while (unit != 0 ? units[ui] != unit : (bytes >= 10000 || (bytes != 0 && (bytes & 1023) == 0))) {
    ui++;
    if (ui >= num_units) {
      // We probably got an unknown unit. Fall back to a natural unit,
      // but leave a hint that something's wrong.
      ZX_DEBUG_ASSERT(str_size > 1);
      *str++ = '?';
      str_size--;
      unit = 0;
      bytes = orig_bytes;
      goto retry;
    }
    bytes /= 1024;
    divisor *= 1024;
  }

  // If the chosen divisor divides the input value evenly, don't print out a
  // fractional part.
  if (orig_bytes % divisor == 0) {
    snprintf(str, str_size, "%zu%c", bytes, units[ui]);
    return orig_str;
  }

  // We don't have an exact number, so print one unit of precision.
  //
  // Ideally we could just calculate:
  //
  //   sprintf("%0.1f\n", (double)orig_bytes / divisor)
  //
  // but want to avoid floating point. Instead, we separately calculate the
  // two parts using integer arithmetic.
  size_t int_part = orig_bytes / divisor;
  size_t fractional_part = rounding_divide((orig_bytes % divisor) * 10, divisor);
  if (fractional_part >= 10) {
    // the fractional part rounded up to 10: carry it over to the integer part.
    fractional_part = 0;
    int_part++;
  }
  snprintf(str, str_size, "%zu.%1zu%c", int_part, fractional_part, units[ui]);

  return orig_str;
}

char* format_size(char* str, size_t str_size, size_t bytes) {
  return format_size_fixed(str, str_size, bytes, 0);
}
