// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/fidl/cpp/message.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

namespace fidl {

// A builder for FIDL messages that owns the memory for the message.
//
// A |MessageBuilder| is a |Builder| that uses the heap to back the memory for
// the message. If you wish to manage the memory yourself, you can use |Builder|
// and |Message| directly.
//
// Upon creation, the |MessageBuilder| creates a FIDL message header, which you
// can modify using |header()|.
class MessageBuilder : public Builder {
public:
    // Creates a |MessageBuilder| for the given |type| that allocates buffers
    // for message of the given capacities.
    //
    // The bytes buffer is initialied by adding a |fidl_message_header_t|
    // header.
    //
    // The buffers are freed when the |MessageBuilder| is destructed.
    explicit MessageBuilder(
        const fidl_type_t* type,
        uint32_t bytes_capacity = ZX_CHANNEL_MAX_MSG_BYTES,
        uint32_t handles_capacity = ZX_CHANNEL_MAX_MSG_HANDLES);

    // The memory that backs the message is freed by this destructor.
    ~MessageBuilder();

    // The type of the message payload this object is building.
    const fidl_type_t* type() const { return type_; }

    // The header for the message.
    //
    // The message header is allocated by the |MessageBuilder| itself.
    fidl_message_header_t* header() const {
        return reinterpret_cast<fidl_message_header_t*>(buffer());
    }

    // Encodes a message of the given |type|.
    //
    // The memory that backs the message returned by this function is owned by
    // the |MessageBuilder|, which means the |MessageBuilder| must remain alive
    // as long as the |Message| object is in use.
    //
    // The |message| parameter might be modified even if this method returns an
    // error.
    zx_status_t Encode(Message* message_out, const char** error_msg_out);

    // Resets all the data in the |MessageBuffer|.
    //
    // The underlying buffer is retained and reused. The next object will be
    // allocated at the start of the buffer.
    void Reset();

private:
    const fidl_type_t* type_;
    MessageBuffer buffer_;
};

} // namespace fidl
