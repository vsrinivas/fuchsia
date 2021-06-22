// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <pretty/cpp/sizes.h>
#include <pretty/sizes.h>

namespace pretty {

std::string_view FormattedBytes::ToString(SizeUnit unit) {
  using namespace std::string_view_literals;

  switch (unit) {
    case SizeUnit::kAuto:
      break;
    case SizeUnit::kBytes:
      return "B"sv;
    case SizeUnit::kKiB:
      return "k"sv;
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
  static const char units[] = "BkMGTPE";
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
