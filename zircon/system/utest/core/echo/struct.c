// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "struct.h"

#include <stdint.h>

_Static_assert(sizeof(mojo_struct_header_t) == 8u, "mojo_struct_header_t should be 8 bytes");

bool mojo_validate_struct_header(const void* data, size_t size) {
    if (size < sizeof(mojo_struct_header_t) || size > UINT32_MAX) {
        return false;
    }

    const mojo_struct_header_t* header = (const mojo_struct_header_t*)data;

    if (header->num_bytes < sizeof(mojo_struct_header_t) || header->num_bytes > size) {
        return false;
    }

    return true;
}
