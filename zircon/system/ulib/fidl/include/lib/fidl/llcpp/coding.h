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

namespace internal {

template <typename Sub>
struct FromFailureMixin {
  // Initialize ourself from one of EncodeResult, DecodeResult, LinearizeResult, in the case of
  // error hence there is no message.
  template <typename SomeResult>
  static Sub FromFailure(SomeResult failure) {
    ZX_DEBUG_ASSERT(failure.status != ZX_OK);
    return Sub(failure.status, failure.error);
  }
};

}  // namespace internal

// The table of any FIDL method with zero in/out parameters.
extern "C" const fidl_type_t _llcpp_coding_AnyZeroArgMessageTable;

// Holds a |DecodedMessage| in addition to |status| and |error|.
// This is typically the return type of fidl::Decode and FIDL methods which require
// a decode step for the response.
// If |status| is ZX_OK, |message| contains a valid decoded message of type FidlType.
// Otherwise, |error| contains a human-readable string for debugging purposes.
template <typename FidlType>
struct DecodeResult final : internal::FromFailureMixin<DecodeResult<FidlType>> {
  zx_status_t status = ZX_ERR_INTERNAL;
  const char* error = nullptr;
  DecodedMessage<FidlType> message;

  // Convenience accessor for the FIDL message pointer.
  // Asserts that the decoding was successful.
  FidlType* Unwrap() {
    ZX_DEBUG_ASSERT(status == ZX_OK);
    return message.message();
  }

  DecodeResult() = default;

  DecodeResult(zx_status_t status, const char* error,
               DecodedMessage<FidlType> message = DecodedMessage<FidlType>())
      : status(status), error(error), message(std::move(message)) {
    ZX_DEBUG_ASSERT(status != ZX_OK || this->message.is_valid());
  }
};

// Holds a |EncodedMessage| in addition to |status| and |error|.
// This is typically the return type of fidl::Encode and other FIDL methods which
// have encoding as the last step.
// If |status| is ZX_OK, |message| contains a valid encoded message of type FidlType.
// Otherwise, |error| contains a human-readable string for debugging purposes.
template <typename FidlType>
struct EncodeResult final : internal::FromFailureMixin<EncodeResult<FidlType>> {
  zx_status_t status = ZX_ERR_INTERNAL;
  const char* error = nullptr;
  EncodedMessage<FidlType> message;

  EncodeResult() = default;

  EncodeResult(zx_status_t status, const char* error,
               EncodedMessage<FidlType> message = EncodedMessage<FidlType>())
      : status(status), error(error), message(std::move(message)) {}
};

// Holds a |DecodedMessage| in addition to |status| and |error|.
// This is typically the return type of fidl::Linearize and other FIDL methods which
// have linearization as the last step.
// If |status| is ZX_OK, |message| contains a valid message in decoded form, of type FidlType.
// Otherwise, |error| contains a human-readable string for debugging purposes.
template <typename FidlType>
struct LinearizeResult final : internal::FromFailureMixin<LinearizeResult<FidlType>> {
  zx_status_t status = ZX_ERR_INTERNAL;
  const char* error = nullptr;
  DecodedMessage<FidlType> message;

  LinearizeResult() = default;

  LinearizeResult(zx_status_t status, const char* error,
                  DecodedMessage<FidlType> message = DecodedMessage<FidlType>())
      : status(status), error(error), message(std::move(message)) {
    ZX_DEBUG_ASSERT(status != ZX_OK || this->message.is_valid());
  }
};

// Consumes an encoded message object containing FIDL encoded bytes and handles.
// Uses the FIDL encoding tables to deserialize the message in-place.
// If the message is invalid, discards the buffer and returns an error.
template <typename FidlType>
DecodeResult<FidlType> Decode(EncodedMessage<FidlType> msg) {
  static_assert(IsFidlType<FidlType>::value, "FIDL type required");
  static_assert(FidlType::Type != nullptr, "FidlType should have a coding table");
  DecodeResult<FidlType> result;

  // Perform in-place decoding
  fidl_trace(WillLLCPPDecode, FidlType::Type, msg.bytes().data(), msg.bytes().actual(),
             msg.handles().actual());
  result.status = fidl_decode(FidlType::Type, msg.bytes().data(), msg.bytes().actual(),
                              msg.handles().data(), msg.handles().actual(), &result.error);
  fidl_trace(DidLLCPPDecode);

  // Clear out |msg| independent of success or failure
  BytePart bytes = msg.ReleaseBytesAndHandles();
  if (result.status == ZX_OK) {
    result.message.Reset(std::move(bytes));
  } else {
    result.message.Reset(BytePart());
  }
  return result;
}

// Serializes the content of the message in-place.
// The message's contents are always consumed by this operation, even in case of an error.
template <typename FidlType>
EncodeResult<FidlType> Encode(DecodedMessage<FidlType> msg) {
  static_assert(IsFidlType<FidlType>::value, "FIDL type required");
  static_assert(FidlType::Type != nullptr, "FidlType should have a coding table");
  EncodeResult<FidlType> result;
  result.message.bytes() = std::move(msg.bytes_);
  uint32_t actual_handles = 0;

  fidl_trace(WillLLCPPInPlaceEncode);
  result.status = fidl_encode(FidlType::Type, result.message.bytes().data(),
                              result.message.bytes().actual(), result.message.handles().data(),
                              result.message.handles().capacity(), &actual_handles, &result.error);
  fidl_trace(DidLLCPPInPlaceEncode, FidlType::Type, result.message.bytes().data(),
             result.message.bytes().actual(), actual_handles);

  result.message.handles().set_actual(actual_handles);
  return result;
}

template <typename FidlType>
EncodeResult<FidlType> LinearizeAndEncode(FidlType* value, BytePart bytes) {
  static_assert(IsFidlType<FidlType>::value, "FIDL type required");
  static_assert(FidlType::Type != nullptr, "FidlType should have a coding table");
  EncodeResult<FidlType> result;
  uint32_t num_bytes_actual;
  uint32_t num_handles_actual;
  result.message.bytes() = std::move(bytes);
  fidl_trace(WillLLCPPLinearizeAndEncode);
  result.status = fidl_linearize_and_encode(
      FidlType::Type, value, result.message.bytes().data(), result.message.bytes().capacity(),
      result.message.handles().data(), result.message.handles().capacity(), &num_bytes_actual,
      &num_handles_actual, &result.error);
  fidl_trace(DidLLCPPLinearizeAndEncode, FidlType::Type, result.message.bytes().data(),
             num_bytes_actual, num_handles_actual);
  if (result.status != ZX_OK) {
    return result;
  }
  result.message.bytes().set_actual(num_bytes_actual);
  result.message.handles().set_actual(num_handles_actual);
  return result;
}

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

template <typename FidlType>
DecodeResult<FidlType> DecodeAs(fidl_msg_t* msg) {
  static_assert(IsFidlMessage<FidlType>::value, "FIDL transactional message type required");
  if (msg->num_handles > EncodedMessage<FidlType>::kResolvedMaxHandles) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    return DecodeResult<FidlType>(ZX_ERR_INVALID_ARGS, "too many handles");
  }
  return fidl::Decode(fidl::EncodedMessage<FidlType>(msg));
}

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

// Encode and write |decoded_msg| down a channel. Used for sending one-way calls and events.
template <typename FidlType>
zx_status_t Write(const zx::unowned_channel& chan, DecodedMessage<FidlType> decoded_msg) {
  static_assert(IsFidlMessage<FidlType>::value, "FIDL transactional message type required");
  fidl::EncodeResult<FidlType> encode_result = fidl::Encode(std::move(decoded_msg));
  if (encode_result.status != ZX_OK) {
    return encode_result.status;
  }
  return Write(chan, std::move(encode_result.message));
}

// Encode and write |decoded_msg| down a channel. Used for sending one-way calls and events.
template <typename FidlType>
zx_status_t Write(const zx::channel& chan, DecodedMessage<FidlType> decoded_msg) {
  return Write(zx::unowned_channel(chan), std::move(decoded_msg));
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
