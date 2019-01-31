// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "message.h"

#include <stdint.h>

_Static_assert(sizeof(mojo_message_header_t) == 16u, "mojo_message_header_t should be 16 bytes");

_Static_assert(sizeof(mojo_message_header_with_request_id_t) == 24u,
               "mojo_message_header_t should be 24 bytes");

bool mojo_validate_message_header(const mojo_struct_header_t* header, size_t size) {
    if (header->num_bytes < sizeof(mojo_message_header_t) || size < sizeof(mojo_message_header_t) ||
        size > UINT32_MAX) {
        return false;
    }

    const mojo_message_header_t* message_header = (const mojo_message_header_t*)header;

    // Message expects response and message is response flags are mutually
    // exclusive.
    if ((message_header->flags & MOJO_MESSAGE_HEADER_FLAGS_EXPECTS_RESPONSE) &&
        (message_header->flags & MOJO_MESSAGE_HEADER_FLAGS_IS_RESPONSE)) {
        return false;
    }

    if (header->version == 0u) {
        if (header->num_bytes != sizeof(mojo_message_header_t)) {
            return false;
        }

        // Version 0 has no request id and should not have either of these flags.
        if ((message_header->flags & MOJO_MESSAGE_HEADER_FLAGS_EXPECTS_RESPONSE) ||
            (message_header->flags & MOJO_MESSAGE_HEADER_FLAGS_IS_RESPONSE)) {
            return false;
        }
    } else if (header->version == 1u) {
        if (header->num_bytes != sizeof(mojo_message_header_with_request_id_t)) {
            return false;
        }
    }
    // Accept unknown versions of the message header to be future-proof.

    return true;
}
