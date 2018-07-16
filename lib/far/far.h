// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FAR_FAR_H_
#define GARNET_LIB_FAR_FAR_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __Fuchsia__
#include <zircon/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct far_reader* far_reader_t;

bool far_reader_create(far_reader_t* reader);

bool far_reader_destroy(far_reader_t reader);

// Takes ownership of the file descriptor.
//
// The file descriptor will be closed during far_reader_destroy.
bool far_reader_read_fd(far_reader_t reader, int fd);

bool far_reader_get_count(far_reader_t reader, uint64_t* count);

bool far_reader_get_index(far_reader_t reader, const char* path,
                          size_t path_length, uint64_t* index);

// The memory pointed to by path is valid until |reader| is destroyed.
bool far_reader_get_path(far_reader_t reader, uint64_t index, const char** path,
                         size_t* path_length);

bool far_reader_get_content(far_reader_t reader, uint64_t index,
                            uint64_t* offset, uint64_t* length);

#ifdef __cplusplus
}
#endif

#endif  // GARNET_LIB_FAR_FAR_H_
