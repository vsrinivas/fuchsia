// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message_part.h>
#include <zircon/fidl.h>

namespace fidl {

// A FIDL message.
//
// A FIDL message has two parts: the bytes and the handles. The bytes are
// divided into a header (of type fidl_message_header_t) and a payload, which
// follows the header.
//
// A Message object does not own the storage for the message parts.
class Message {
public:
    // Creates a message without any storage.
    Message();

    // Creates a message whose storage is backed by |bytes| and |handles|.
    //
    // The constructed |Message| object does not take ownership of the given
    // storage.
    Message(BytePart bytes, HandlePart handles);

    ~Message();

    Message(const Message& other) = delete;
    Message& operator=(const Message& other) = delete;

    Message(Message&& other);
    Message& operator=(Message&& other);

    // Whether the message has enough bytes to contain a fidl_message_header_t.
    bool has_header() const {
        return bytes_.actual() >= sizeof(fidl_message_header_t);
    }

    // The header at the start of the message.
    //
    // Valid only if has_header().
    const fidl_message_header_t& header() const {
        return *reinterpret_cast<fidl_message_header_t*>(bytes_.data());
    }
    fidl_message_header_t& header() {
        return *reinterpret_cast<fidl_message_header_t*>(bytes_.data());
    }

    // The transaction ID in the message header.
    //
    // Valid only if has_header().
    zx_txid_t txid() const { return header().txid; }
    void set_txid(zx_txid_t txid) { header().txid = txid; }

    // The flags in the message header.
    //
    // Valid only if has_header().
    uint32_t flags() const { return header().flags; }

    // The flags in the message header.
    //
    // Valid only if has_header().
    uint32_t ordinal() const { return header().ordinal; }

    // The message payload that follows the header.
    //
    // Valid only if has_header().
    BytePart payload() const {
        constexpr uint32_t n = sizeof(fidl_message_header_t);
        return BytePart(bytes_.data() + n, bytes_.capacity() - n, bytes_.actual() - n);
    }

    // The message payload that follows the header interpreted as the given type.
    //
    // Valid only if has_header().
    template <typename T>
    T* GetPayloadAs() const {
        return reinterpret_cast<T*>(bytes_.data() + sizeof(fidl_message_header_t));
    }

    // The storage for the bytes of the message.
    BytePart& bytes() { return bytes_; }
    const BytePart& bytes() const { return bytes_; }

    // The storage for the handles of the message.
    //
    // When the message is encoded, the handle values are stored in this part of
    // the message. When the message is decoded, this part of the message is
    // empty and the handle values are stored in the bytes().
    HandlePart& handles() { return handles_; }
    const HandlePart& handles() const { return handles_; }

    // Encodes the message in-place.
    //
    // The message must previously have been in a decoded state, for example,
    // either by being built in a decoded state using a |Builder| or having been
    // decoded using the |Decode| method.
    zx_status_t Encode(const fidl_type_t* type, const char** error_msg_out);

    // Decodes the message in-place.
    //
    // The message must previously have been in an encoded state, for example,
    // either by being read from a zx_channel_t or having been encoded using the
    // |Encode| method.
    zx_status_t Decode(const fidl_type_t* type, const char** error_msg_out);

    // Validates the message in-place.
    //
    // The message must already be in an encoded state, for example, either by
    // being read from a zx_channel_t or having been created in that state.
    //
    // Does not modify the message.
    zx_status_t Validate(const fidl_type_t* type, const char** error_msg_out) const;

    // Read a message from the given channel.
    //
    // The bytes read from the channel are stored in bytes() and the handles
    // read from the channel are stored in handles(). Existing data in these
    // buffers is overwritten.
    zx_status_t Read(zx_handle_t channel, uint32_t flags);

    // Writes a message to the given channel.
    //
    // The bytes stored in bytes() are written to the channel and the handles
    // stored in handles() are written to the channel.
    //
    // If this method returns ZX_OK, handles() will be empty because they were
    // consumed by this operation.
    zx_status_t Write(zx_handle_t channel, uint32_t flags);

    // Issues a synchronous send and receive transaction on the given channel.
    //
    // The bytes stored in bytes() are written to the channel and the handles
    // stored in handles() are written to the channel. The bytes read from the
    // channel are stored in response->bytes() and the handles read from the
    // channel are stored in response->handles().
    //
    // If this method returns ZX_OK, handles() will be empty because they were
    // consumed by this operation.
    zx_status_t Call(zx_handle_t channel, uint32_t flags, zx_time_t deadline,
                     zx_status_t* read_status, Message* response);

    // Stop tracking the handles in stored in handles(), without closing them.
    //
    // Typically, these handles will be extracted during decode or the
    // message's destructor, so this function will be unnecessary. However,
    // for clients of ulib/fidl which decode message manually, this function
    // is necessary to prevent extracted handles from being closed.
    void ClearHandlesUnsafe();

private:
    BytePart bytes_;
    HandlePart handles_;
};

} // namespace fidl
