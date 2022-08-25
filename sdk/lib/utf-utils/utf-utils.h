// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UTF_UTILS_UTF_UTILS_H_
#define LIB_UTF_UTILS_UTF_UTILS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns whether the string pointed to by `data` of length `size` is valid UTF-8.
bool utfutils_is_valid_utf8(const char* data, size_t size);

// Simultaneously copies `size` bytes of data from `src` to `dst` and validates whether the data is
// valid UTF-8.
//
// If the data is not valid UTF-8, the number of bytes copied to `dst` is explicitly unspecified.
bool utfutils_validate_and_copy_utf8(const char* src, char* dst, size_t size);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LIB_UTF_UTILS_UTF_UTILS_H_
