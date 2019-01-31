// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_C_BINDINGS_MESSAGE_H_
#define MOJO_PUBLIC_C_BINDINGS_MESSAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "struct.h"

// These bits may be set in the |flags| field of a Mojo message header.
#define MOJO_MESSAGE_HEADER_FLAGS_EXPECTS_RESPONSE (1 << 0u)
#define MOJO_MESSAGE_HEADER_FLAGS_IS_RESPONSE (1 << 1u)

#ifdef __cplusplus
extern "C" {
#endif

// Validates that the buffer started at a (validated) mojo_struct_header with a
// given size contains a valid mojo message header.
bool mojo_validate_message_header(const mojo_struct_header_t* header, size_t size);

typedef struct mojo_message_header {
    mojo_struct_header_t struct_header;
    uint32_t name;
    uint32_t flags;
} mojo_message_header_t;

typedef struct mojo_message_header_with_request_id {
    mojo_message_header_t message_header;
    uint64_t request_id;
} mojo_message_header_with_request_id_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MOJO_PUBLIC_C_BINDINGS_MESSAGE_H_
