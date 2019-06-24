// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_FILE_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_FILE_UTILS_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Read file at |file_path| into a heap-allocated buffer.
// On success, return true and sets |*file_data| and |*file_size| appropriately.
// The caller is responsible for releasing the memory by calling free().
// Note that |*file_data == NULL && *file_size == 0| will be true for an empty
// file. On failure, return false/errno.
// REQUIRES: |file_path != NULL|, |file_data| != NULL, |file_size| != NULL.
extern bool
file_read(const char * const file_path, void ** const file_data, size_t * const file_size);

// Write |file_size| bytes from |file_data| to a file at |file_path|.
// Return true on success, or false/errno on failure.
extern bool
file_write(const char * const file_path, const void * const file_data, size_t file_size);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_FILE_UTILS_H_
