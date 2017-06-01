// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Utilities for formatting sizes to make them more human-readable.

#pragma once

#include <magenta/compiler.h>

#include <stddef.h>

__BEGIN_CDECLS

// A buffer size (including trailing NUL) that's large enough for
// any value formatted by format_size_fixed().
#define MAX_FORMAT_SIZE_LEN sizeof("18446744073709551616B")

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
// |unit| is the unit to use, one of [BkMGTPE]. If zero, picks a natural
// unit for the size, ensuring at most four whole decimal places. If
// |unit| is unknown, the output will have a '?' prefix but otherwise
// behave the same as |unit==0|.
//
// Returns |str|.
char* format_size_fixed(char* str, size_t str_size, size_t bytes, char unit);

// Calls format_size_fixed() with unit=0, picking a natural unit for the size.
char* format_size(char* str, size_t str_size, size_t bytes);

__END_CDECLS
