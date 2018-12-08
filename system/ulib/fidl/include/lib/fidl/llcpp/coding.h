// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CODING_H_
#define LIB_FIDL_LLCPP_CODING_H_

#include <lib/fidl/llcpp/decoded_message.h>
#include <lib/fidl/llcpp/encoded_message.h>
#include <lib/fidl/llcpp/traits.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#include <lib/zx/channel.h>
#endif

namespace fidl {

template<typename FidlType>
struct DecodeResult {
    zx_status_t status = ZX_ERR_INTERNAL;
    const char* error = nullptr;
    DecodedMessage<FidlType> message;

    DecodeResult() = default;
};

template<typename FidlType>
struct EncodeResult {
    zx_status_t status = ZX_ERR_INTERNAL;
    const char* error = nullptr;
    EncodedMessage<FidlType> message;

    EncodeResult() = default;
};

// Consumes an encoded message object containing FIDL encoded bytes and handles.
// Uses the FIDL encoding tables to deserialize the message in-place.
// If the message is invalid, discards the buffer and returns an error.
template <typename FidlType>
DecodeResult<FidlType> Decode(EncodedMessage<FidlType> msg) {
    DecodeResult<FidlType> result;
    // Perform in-place decoding
    result.status = fidl_decode(FidlType::Type,
                                msg.bytes().data(), msg.bytes().actual(),
                                msg.handles().data(), msg.handles().actual(),
                                &result.error);
    // Clear out |msg| independent of success or failure
    BytePart bytes = msg.ReleaseBytesAndHandles();
    if (result.status == ZX_OK) {
        result.message.Reset(std::move(bytes));
    } else {
        result.message.Reset(BytePart());
    }
    return result;
}

// Serializes the content of the message in-place and stores the result
// in |out_msg|. The message's contents are always consumed by this
// operation, even in case of an error.
template <typename FidlType>
EncodeResult<FidlType> Encode(DecodedMessage<FidlType> msg) {
    EncodeResult<FidlType> result;
    result.status = result.message.Initialize([&msg, &result] (BytePart& msg_bytes,
                                                               HandlePart& msg_handles) {
        msg_bytes = std::move(msg.bytes_);
        uint32_t actual_handles = 0;
        zx_status_t status = fidl_encode(FidlType::Type,
                                         msg_bytes.data(), msg_bytes.actual(),
                                         msg_handles.data(), msg_handles.capacity(),
                                         &actual_handles, &result.error);
        msg_handles.set_actual(actual_handles);
        return status;
    });
    return result;
}

#ifdef __Fuchsia__
// Perform a synchronous FIDL channel call.
// Sends the request message down the channel, then waits for the desired reply message, and
// wraps it in an EncodeResult for the response type.
template <typename RequestType>
EncodeResult<typename RequestType::ResponseType> Call(zx::channel& chan,
                                                      EncodedMessage<RequestType> request,
                                                      BytePart response_buffer) {
    static_assert(IsFidlMessage<RequestType>::value, "FIDL transactional message type required");
    EncodeResult<typename RequestType::ResponseType> result;
    result.message.Initialize([&](BytePart& bytes, HandlePart& handles) {
        bytes = std::move(response_buffer);
        zx_channel_call_args_t args = {
            .wr_bytes = request.bytes().data(),
            .wr_handles = request.handles().data(),
            .rd_bytes = bytes.data(),
            .rd_handles = handles.data(),
            .wr_num_bytes = request.bytes().actual(),
            .wr_num_handles = request.handles().actual(),
            .rd_num_bytes = bytes.capacity(),
            .rd_num_handles = handles.capacity()
        };

        uint32_t actual_num_bytes = 0u;
        uint32_t actual_num_handles = 0u;
        result.status = chan.call(
            0u, zx::time::infinite(), &args, &actual_num_bytes, &actual_num_handles);
        if (result.status != ZX_OK) {
            return;
        }

        bytes.set_actual(actual_num_bytes);
        handles.set_actual(actual_num_handles);
    });
    return result;
}
#endif

} // namespace fidl

#endif  // LIB_FIDL_LLCPP_CODING_H_
