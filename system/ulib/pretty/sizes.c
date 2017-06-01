// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pretty/sizes.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <magenta/assert.h>

char* format_size_fixed(char* str, size_t str_size, size_t bytes, char unit) {
    static const char units[] = "BkMGTPE";
    static int num_units = sizeof(units) - 1;

    if (str_size == 0) {
        // Even if NULL.
        return str;
    }
    MX_DEBUG_ASSERT(str != NULL);
    if (str_size == 1) {
        str[0] = '\0';
        return str;
    }

    char* orig_str = str;
    size_t orig_bytes = bytes;
retry:;
    int ui = 0;
    uint16_t r = 0;
    bool whole = true;
    // If we have a fixed (non-zero) unit, divide until we hit it.
    //
    // Otherwise, divide until we reach a unit that can express the value
    // with 4 or fewer whole digits.
    // - If we can express the value without a fraction (it's a whole
    //   kibi/mebi/gibibyte), use the largest possible unit (e.g., favor
    //   "1M" over "1024k").
    // - Otherwise, favor more whole digits to retain precision (e.g.,
    //   favor "1025k" or "1025.0k" over "1.0M").
    while (unit != 0
               ? units[ui] != unit
               : (bytes >= 10000 || (bytes != 0 && (bytes & 1023) == 0))) {
        ui++;
        if (ui >= num_units) {
            // We probably got an unknown unit. Fall back to a natural unit,
            // but leave a hint that something's wrong.
            MX_DEBUG_ASSERT(str_size > 1);
            *str++ = '?';
            str_size--;
            unit = 0;
            bytes = orig_bytes;
            goto retry;
        }
        if (bytes & 1023) {
            whole = false;
        }
        r = bytes % 1024;
        bytes /= 1024;
    }
    if (whole) {
        snprintf(str, str_size, "%zu%c", bytes, units[ui]);
    } else {
        // r represents the remainder of the most recent division operation.
        // Since we provide a single unit of precision, we can round it based
        // on the second digit and increment bytes in the case that it pushes
        // the final value back over into a whole number.
        unsigned int round_up = ((r % 100) >= 50);
        r = (r / 100) + round_up;
        if (r == 10) {
            bytes++;
            r = 0;
        }
        snprintf(str, str_size, "%zu.%1u%c", bytes, r, units[ui]);
    }
    return orig_str;
}

char* format_size(char* str, size_t str_size, size_t bytes) {
    return format_size_fixed(str, str_size, bytes, 0);
}
