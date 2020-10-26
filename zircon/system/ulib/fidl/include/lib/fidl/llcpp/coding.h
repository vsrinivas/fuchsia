// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CODING_H_
#define LIB_FIDL_LLCPP_CODING_H_

#include <lib/fidl/llcpp/decoded_message.h>
#include <lib/fidl/llcpp/encoded_message.h>
#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/fidl/trace.h>
#include <lib/fidl/txn_header.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>
#endif

namespace fidl {

// The table of any FIDL method with zero in/out parameters.
extern "C" const fidl_type_t _llcpp_coding_AnyZeroArgMessageTable;

#ifdef __Fuchsia__

namespace {

template <bool, typename RequestType, typename ResponseType>
struct MaybeSelectResponseType {
  using type = ResponseType;
};

template <typename RequestType, typename ResponseType>
struct MaybeSelectResponseType<true, RequestType, ResponseType> {
  using type = typename RequestType::ResponseType;
};

}  // namespace

// Write |encoded_msg| down a channel. Used for sending one-way calls and events.
template <typename FidlType>
zx_status_t Write(const zx::unowned_channel& chan, EncodedMessage<FidlType> encoded_msg) {
  static_assert(IsFidlMessage<FidlType>::value, "FIDL transactional message type required");

  fidl_trace(WillLLCPPChannelWrite, nullptr /* type */, encoded_msg.bytes().data(),
             encoded_msg.bytes().actual(), encoded_msg.handles().actual());
  auto status = chan->write(0, encoded_msg.bytes().data(), encoded_msg.bytes().actual(),
                            encoded_msg.handles().data(), encoded_msg.handles().actual());
  fidl_trace(DidLLCPPChannelWrite);

  encoded_msg.ReleaseBytesAndHandles();
  return status;
}

// Write |encoded_msg| down a channel. Used for sending one-way calls and events.
template <typename FidlType>
zx_status_t Write(const zx::channel& chan, EncodedMessage<FidlType> encoded_msg) {
  return Write(zx::unowned_channel(chan), std::move(encoded_msg));
}

// If |RequestType::ResponseType| exists, use that. Otherwise, fallback to |ResponseType|.
template <typename RequestType, typename ResponseType>
struct SelectResponseType {
  using type = typename MaybeSelectResponseType<internal::HasResponseType<RequestType>::value,
                                                RequestType, ResponseType>::type;
};

// Perform a synchronous FIDL channel call. This overload takes a |zx::unowned_channel|.
// Sends the request message down the channel, then waits for the desired reply message, and
// wraps it in an EncodeResult for the response type.
// If |RequestType| is |AnyZeroArgMessage|, the caller may explicitly specify an expected response
// type by overriding the template parameter |ResponseType|.
// The call will block until |deadline|, which defaults to forever. If a |deadline| is specified,
// the call will error with |ZX_ERR_TIMED_OUT| when the deadline has passed without a reply.
template <typename RequestType, typename ResponseType = typename RequestType::ResponseType>
EncodeResult<ResponseType> Call(zx::unowned_channel chan, EncodedMessage<RequestType> request,
                                BytePart response_buffer,
                                zx::time deadline = zx::time::infinite()) {
  static_assert(IsFidlMessage<RequestType>::value, "FIDL transactional message type required");
  static_assert(IsFidlMessage<ResponseType>::value, "FIDL transactional message type required");
  // If |RequestType| has a defined |ResponseType|, ensure it matches the template parameter.
  static_assert(std::is_same<typename SelectResponseType<RequestType, ResponseType>::type,
                             ResponseType>::value,
                "RequestType and ResponseType are incompatible");

  EncodeResult<ResponseType> result;
  result.message.bytes() = std::move(response_buffer);
  uint32_t actual_num_bytes = 0u;
  uint32_t actual_num_handles = 0u;

  zx_channel_call_args_t args = {.wr_bytes = request.bytes().data(),
                                 .wr_handles = request.handles().data(),
                                 .rd_bytes = result.message.bytes().data(),
                                 .rd_handles = result.message.handles().data(),
                                 .wr_num_bytes = request.bytes().actual(),
                                 .wr_num_handles = request.handles().actual(),
                                 .rd_num_bytes = result.message.bytes().capacity(),
                                 .rd_num_handles = result.message.handles().capacity()};

  fidl_trace(WillLLCPPChannelCall, nullptr /* type */, request.bytes().data(),
             request.bytes().actual(), request.handles().actual());
  result.status = chan->call(0u, deadline, &args, &actual_num_bytes, &actual_num_handles);
  fidl_trace(DidLLCPPChannelCall, nullptr /* type */, result.message.bytes().data(),
             actual_num_bytes, actual_num_handles);

  request.ReleaseBytesAndHandles();
  if (result.status == ZX_OK) {
    result.message.handles().set_actual(actual_num_handles);
    result.message.bytes().set_actual(actual_num_bytes);
  }
  return result;
}

// Perform a synchronous FIDL channel call. This overload takes a |zx::channel&|.
// Sends the request message down the channel, then waits for the desired reply message, and
// wraps it in an EncodeResult for the response type.
// If |RequestType| is |AnyZeroArgMessage|, the caller may explicitly specify an expected response
// type by overriding the template parameter |ResponseType|.
// The call will block until |deadline|, which defaults to forever. If a |deadline| is specified,
// the call will error with |ZX_ERR_TIMED_OUT| when the deadline has passed without a reply.
template <typename RequestType, typename ResponseType = typename RequestType::ResponseType>
EncodeResult<ResponseType> Call(zx::channel& chan, EncodedMessage<RequestType> request,
                                BytePart response_buffer,
                                zx::time deadline = zx::time::infinite()) {
  return Call<RequestType, ResponseType>(zx::unowned_channel(chan), std::move(request),
                                         std::move(response_buffer), deadline);
}

// Calculates the maximum possible message size for a FIDL type,
// clamped at the Zircon channel packet size.
// TODO(fxbug.dev/8093): Always request the message context.
template <typename FidlType, const MessageDirection Direction = MessageDirection::kReceiving>
constexpr uint32_t MaxSizeInChannel() {
  return internal::ClampedMessageSize<FidlType, Direction>();
}

#endif

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CODING_H_
