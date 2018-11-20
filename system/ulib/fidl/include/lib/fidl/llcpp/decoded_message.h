// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_DECODED_MESSAGE_H_
#define LIB_FIDL_LLCPP_DECODED_MESSAGE_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/llcpp/encoded_message.h>
#include <lib/fidl/llcpp/traits.h>
#include <type_traits>
#include <zircon/fidl.h>

namespace fidl {

// `DecodedMessage` manages a linearized FIDL message in decoded form.
// It takes care of releasing all handles which were not consumed
// (std::moved from the decoded FIDL struct) when it goes out of scope.
template <typename FidlType>
class DecodedMessage final {
    static_assert(IsFidlType<FidlType>::value, "Only FIDL types allowed here");
    static_assert(FidlType::MaxSize > 0, "Positive message size");

public:
    // Instantiates an empty message.
    // To populate this message, decode from an EncodedMessage object.
    DecodedMessage() = default;

    // Instantiates a DecodedMessage which points to a buffer region with caller-managed memory.
    // The buffer region is assumed to contain a linearized FIDL message with valid pointers.
    // This does not take ownership of that buffer region.
    // But it does take ownership of the handles within the buffer.
    DecodedMessage(BytePart bytes) :
        bytes_(std::move(bytes)) { }

    DecodedMessage(DecodedMessage&& other) = default;

    DecodedMessage& operator=(DecodedMessage&& other) = default;

    DecodedMessage(const DecodedMessage& other) = delete;

    DecodedMessage& operator=(const DecodedMessage& other) = delete;

    ~DecodedMessage() {
        CloseHandles();
    }

    // Keeps track of a new buffer region with caller-managed memory.
    // The buffer region is assumed to contain a linearized FIDL message with valid pointers.
    // This does not take ownership of that buffer region.
    // But it does take ownership of the handles within the buffer.
    void Reset(BytePart bytes) {
        CloseHandles();
        bytes_ = std::move(bytes);
    }

    // Consumes an encoded message object containing FIDL encoded bytes and handles.
    // The current buffer region in DecodedMessage is always released.
    // Uses the FIDL encoding tables to deserialize the message in-place.
    // If the message is invalid, discards the buffer and returns an error.
    zx_status_t DecodeFrom(EncodedMessage<FidlType>* msg, const char** out_error_msg) {
        // Clear any existing message.
        CloseHandles();
        bytes_ = BytePart();
        zx_status_t status = fidl_decode(FidlType::type,
                                         msg->bytes().data(), msg->bytes().actual(),
                                         msg->handles().data(), msg->handles().actual(),
                                         out_error_msg);
        // Clear out |msg| independent of success or failure
        BytePart bytes = msg->ReleaseBytesAndHandles();
        if (status == ZX_OK) {
            Reset(std::move(bytes));
        } else {
            Reset(BytePart());
        }
        return status;
    }

    // Serializes the content of the message in-place and stores the result
    // in |out_msg|. The message's contents are always consumed by this
    // operation, even in case of an error.
    zx_status_t EncodeTo(EncodedMessage<FidlType>* out_msg, const char** out_error_msg) {
        return out_msg->Initialize([this, &out_error_msg] (BytePart& msg_bytes,
                                                           HandlePart& msg_handles) {
            msg_bytes = std::move(bytes_);
            uint32_t actual_handles = 0;
            zx_status_t status = fidl_encode(FidlType::type,
                                             msg_bytes.data(), msg_bytes.actual(),
                                             msg_handles.data(), msg_handles.capacity(),
                                             &actual_handles, out_error_msg);
            msg_handles.set_actual(actual_handles);
            return status;
        });
    }

    // Accesses the FIDL message by reinterpreting the buffer pointer.
    // Returns nullptr if there is no message.
    FidlType* message() const {
        return reinterpret_cast<FidlType*>(bytes_.data());
    }

private:
    // Use the FIDL encoding tables for |FidlType| to walk the message and
    // destroy the handles it contains.
    void CloseHandles() {
#ifdef __Fuchsia__
        if (bytes_.data()) {
            fidl_close_handles(FidlType::type, bytes_.data(), bytes_.actual(), nullptr);
        }
#endif
    }

    // The contents of the decoded message.
    BytePart bytes_;
};

}  // namespace fidl

#endif // LIB_FIDL_LLCPP_DECODED_MESSAGE_H_
