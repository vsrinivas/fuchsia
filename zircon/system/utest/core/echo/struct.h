// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_C_BINDINGS_STRUCT_H_
#define MOJO_PUBLIC_C_BINDINGS_STRUCT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Validates that a given buffer has a mojo struct header and that the size of
// the struct in the header matches the size of the buffer.
bool mojo_validate_struct_header(const void* data, size_t size);

typedef struct struct_header {
    uint32_t num_bytes;
    uint32_t version;
} mojo_struct_header_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MOJO_PUBLIC_C_BINDINGS_STRUCT_H_
