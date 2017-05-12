// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Utilities for formatting sizes to make them more human-readable.

#pragma once

#include <magenta/compiler.h>

#include <stddef.h>

__BEGIN_CDECLS

// A buffer size (including trailing NUL) that's large enough for
// any value formatted by format_size().
#define MAX_FORMAT_SIZE_LEN sizeof("1234.0k")

// Formats |bytes| as a human-readable string like "123.4k".
// Units are in powers of 1024, so "k" is technically "kiB", etc.
// Values smaller than "k" have the suffix "B".
//
// Exact multiples of a unit are displayed without a decimal;
// e.g., "17k" means the value is exactly 17 * 1024.
//
// Otherwise, a decimal is present; e.g., "17.0k" means the value
// is (17 * 1024) +/- epsilon.
//
// |str_size| is the size of the memory pointed to by |str|,
// including the trailing NUL.
//
// Returns |str|.
char* format_size(char* str, size_t str_size, size_t bytes);

__END_CDECLS
