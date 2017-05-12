// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pretty/sizes.h>

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

char* format_size(char* str, size_t str_size, size_t bytes) {
    static const char units[] = "BkMGTPE";
    static int num_units = sizeof(units) - 1;

    if (str_size == 0) {
        // Even if NULL.
        return str;
    }
    assert(str != NULL);

    int ui = 0;
    double db = bytes;
    bool whole = true;
    // Divide until we reach a unit that can express the value
    // with 4 or fewer whole digits.
    // - If we can express the value without a fraction (it's a whole
    //   kibi/mebi/gibibyte), use the largest possible unit (e.g., favor
    //   "1M" over "1024k").
    // - Otherwise, favor more whole digits to retain precision (e.g.,
    //   favor "1025k" or "1025.0k" over "1.0M").
    while (bytes >= 10000 || (bytes != 0 && (bytes & 1023) == 0)) {
        ui++;
        assert(ui < num_units); // Will never exceed "E" with a 64-bit number.
        db /= 1024;
        if (bytes & 1023) {
            whole = false;
        }
        bytes /= 1024;
    }
    if (whole) {
        snprintf(str, str_size, "%zd%c", bytes, units[ui]);
    } else {
        snprintf(str, str_size, "%.1f%c", db, units[ui]);
    }
    return str;
}
