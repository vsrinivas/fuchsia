// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CODING_H_
#define LIB_FIDL_LLCPP_CODING_H_

#include <lib/fidl/llcpp/decoded_message.h>
#include <lib/fidl/llcpp/encoded_message.h>
#include <lib/fidl/llcpp/response_storage.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/fidl/runtime_flag.h>
#include <lib/fidl/transformer.h>
#include <lib/fidl/txn_header.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>
#endif

namespace fidl {

namespace internal {

// Predefined error messages in the binding
constexpr char kErrorRequestBufferTooSmall[] = "request buffer too small";
constexpr char kErrorWriteFailed[] = "failed writing to the underlying transport";

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

// The request/response type of any FIDL method with zero in/out parameters.
struct AnyZeroArgMessage final {
  FIDL_ALIGNDECL
  fidl_message_header_t _hdr;

  static constexpr const fidl_type_t* Type = nullptr;
  static constexpr const fidl_type_t* AltType = nullptr;
  static constexpr uint32_t MaxNumHandles = 0;
  static constexpr uint32_t PrimarySize = sizeof(fidl_message_header_t);
  static constexpr uint32_t MaxOutOfLine = 0;
  static constexpr uint32_t AltPrimarySize = sizeof(fidl_message_header_t);
  static constexpr uint32_t AltMaxOutOfLine = 0;
  static constexpr bool HasFlexibleEnvelope = false;
  static constexpr bool ContainsUnion = false;
};

template <>
struct IsFidlType<AnyZeroArgMessage> : public std::true_type {};
template <>
struct IsFidlMessage<AnyZeroArgMessage> : public std::true_type {};

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
  DecodeResult<FidlType> result;
  // Perform in-place decoding
  if (NeedsEncodeDecode<FidlType>::value) {
    result.status = fidl_decode(FidlType::Type, msg.bytes().data(), msg.bytes().actual(),
                                msg.handles().data(), msg.handles().actual(), &result.error);
  } else {
    // Boring type does not need decoding
    if (msg.bytes().actual() != FidlType::PrimarySize) {
      result.error = "invalid size decoding";
    } else if (msg.handles().actual() != 0) {
      result.error = "invalid handle count decoding";
    } else {
      result.status = ZX_OK;
    }
  }
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
  EncodeResult<FidlType> result;
  result.status =
      result.message.Initialize([&msg, &result](BytePart* out_msg_bytes, HandlePart* msg_handles) {
        *out_msg_bytes = std::move(msg.bytes_);
        if (NeedsEncodeDecode<FidlType>::value) {
          uint32_t actual_handles = 0;
          zx_status_t status = fidl_encode(FidlType::Type, out_msg_bytes->data(),
                                           out_msg_bytes->actual(), msg_handles->data(),
                                           msg_handles->capacity(), &actual_handles, &result.error);
          msg_handles->set_actual(actual_handles);
          return status;
        } else {
          if (out_msg_bytes->actual() != FidlAlign(FidlType::PrimarySize)) {
            result.error = "invalid size encoding";
            return ZX_ERR_INVALID_ARGS;
          }
          memset(out_msg_bytes->data() + FidlType::PrimarySize, 0,
                 out_msg_bytes->actual() - FidlType::PrimarySize);
          // Boring type does not need encoding
          msg_handles->set_actual(0);
          return ZX_OK;
        }
      });
  return result;
}

// Linearizes the contents of the message starting at |value|, into a continuous |bytes| buffer.
// Upon success, the handles in the source messages will be moved into |bytes|.
// the remaining contents in the source messages are otherwise untouched.
// In case of any failure, the handles from |value| will stay intact.
template <typename FidlType>
LinearizeResult<FidlType> Linearize(FidlType* value, BytePart bytes) {
  static_assert(IsFidlType<FidlType>::value, "FIDL type required");
  static_assert(FidlType::Type != nullptr, "FidlType should have a coding table");
  static_assert(FidlType::MaxOutOfLine > 0,
                "Only types with out-of-line members need linearization");
  LinearizeResult<FidlType> result;
  uint32_t num_bytes_actual;
  result.status = fidl_linearize(FidlType::Type, value, bytes.data(), bytes.capacity(),
                                 &num_bytes_actual, &result.error);
  if (result.status != ZX_OK) {
    return result;
  }
  bytes.set_actual(num_bytes_actual);
  result.message = DecodedMessage<FidlType>(std::move(bytes));
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
  auto status = chan->write(0, encoded_msg.bytes().data(), encoded_msg.bytes().actual(),
                            encoded_msg.handles().data(), encoded_msg.handles().actual());
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

  result.message.Initialize([&response_buffer, &request, &chan, &result, deadline](
                                BytePart* out_bytes, HandlePart* handles) {
    *out_bytes = std::move(response_buffer);
    uint32_t actual_num_bytes = 0u;
    uint32_t actual_num_handles = 0u;

    if constexpr (ResponseType::ContainsUnion) {
      // Allocate transformer buffer, in anticipation that the message received might be in an
      // alternate wire-format.
      auto max = [](uint32_t a, uint32_t b) { return a > b ? a : b; };
      constexpr uint32_t kMaxSizeForAllFormats =
          max(fidl::internal::ClampedMessageSize<ResponseType, MessageDirection::kReceiving,
                                                 internal::WireFormatGuide::kCurrent>(),
              fidl::internal::ClampedMessageSize<ResponseType, MessageDirection::kReceiving,
                                                 internal::WireFormatGuide::kAlternate>());
      fidl::internal::ByteStorage<kMaxSizeForAllFormats> transformer_src_storage;
      uint8_t* transformer_src = transformer_src_storage.buffer().data();

      // Perform the call, receiving into transformer buffer
      zx_channel_call_args_t args = {.wr_bytes = request.bytes().data(),
                                     .wr_handles = request.handles().data(),
                                     .rd_bytes = transformer_src,
                                     .rd_handles = handles->data(),
                                     .wr_num_bytes = request.bytes().actual(),
                                     .wr_num_handles = request.handles().actual(),
                                     .rd_num_bytes = kMaxSizeForAllFormats,
                                     .rd_num_handles = handles->capacity()};

      result.status = chan->call(0u, deadline, &args, &actual_num_bytes, &actual_num_handles);
      request.ReleaseBytesAndHandles();
      if (result.status != ZX_OK) {
        return;
      }
      handles->set_actual(actual_num_handles);

      // Determine if we need to perform transformation
      if (actual_num_bytes < sizeof(fidl_message_header_t)) {
        result.status = ZX_ERR_INVALID_ARGS;
        return;
      }
      fidl_transformation_t transformation;
      if (!fidl_should_decode_union_from_xunion(
              reinterpret_cast<fidl_message_header_t*>(transformer_src))) {
        transformation = FIDL_TRANSFORMATION_OLD_TO_V1;
      } else {
        transformation = FIDL_TRANSFORMATION_NONE;
      }

      // Transform into user buffer
      result.status = fidl_transform(transformation, ResponseType::AltType, transformer_src,
                                     actual_num_bytes, out_bytes->data(), out_bytes->capacity(),
                                     &actual_num_bytes, &result.error);
      if (result.status != ZX_OK) {
        return;
      }
    } else {
      zx_channel_call_args_t args = {.wr_bytes = request.bytes().data(),
                                     .wr_handles = request.handles().data(),
                                     .rd_bytes = out_bytes->data(),
                                     .rd_handles = handles->data(),
                                     .wr_num_bytes = request.bytes().actual(),
                                     .wr_num_handles = request.handles().actual(),
                                     .rd_num_bytes = out_bytes->capacity(),
                                     .rd_num_handles = handles->capacity()};

      result.status = chan->call(0u, deadline, &args, &actual_num_bytes, &actual_num_handles);
      request.ReleaseBytesAndHandles();
      if (result.status != ZX_OK) {
        return;
      }
      handles->set_actual(actual_num_handles);
    }

    out_bytes->set_actual(actual_num_bytes);
  });
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
// TODO(FIDL-771): Always request the message context.
template <typename FidlType, const MessageDirection Direction = MessageDirection::kReceiving>
constexpr uint32_t MaxSizeInChannel() {
  return internal::ClampedMessageSize<FidlType, Direction>();
}

#endif

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CODING_H_
