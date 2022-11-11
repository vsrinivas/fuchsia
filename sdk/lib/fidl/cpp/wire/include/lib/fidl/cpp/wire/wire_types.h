// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_TYPES_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_TYPES_H_

#include <lib/fidl/cpp/wire/decoded_value.h>
#include <lib/fidl/cpp/wire/envelope.h>
#include <lib/fidl/cpp/wire/incoming_message.h>
#include <lib/fidl/cpp/wire/message_storage.h>
#include <lib/fidl/cpp/wire/object_view.h>
#include <lib/fidl/cpp/wire/outgoing_message.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/wire_coding_traits.h>
#include <lib/fidl/cpp/wire_format_metadata.h>
#include <lib/fit/inline_any.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#else
#include <lib/fidl/cpp/wire/internal/transport_channel_host.h>
#endif  // __Fuchsia__

// # Wire domain objects
//
// This header contains forward definitions that are part of wire domain
// objects. The code generator should populate the implementation by generating
// template specializations for each FIDL data type.
namespace fidl {

// |WireTableFrame| stores the envelope header for each field in a table.
// In their current wire format representation, a table is a vector of
// envelopes. The table frame is the vector body portion of the table.
//
// It is recommended that table frames are managed automatically using arenas.
// Only directly construct a table frame when performance is key and arenas are
// insufficient. Once created, a frame can only be used for one single table.
template <typename FidlTable>
struct WireTableFrame;

// |WireTableBuilder| is a helper class for building tables. They're created by
// calling the static |Build(AnyArena&)| on a FIDL wire table type. The table's
// frame and members will be allocated from supplied arena.
//
// Table members are set by passing constructor arguments or |ObjectView|s into
// a builder method named for the member.
//
// To get the built table call |Build()|. The builder must not be used after
// |Build()| has been called.
template <typename FidlTable>
class WireTableBuilder;

// |WireTableExternalBuilder| is a low-level helper class for building tables.
// They're created by calling the static
// |Build(fidl::ObjectView<fidl::WireTableFrame<T>>)| on a FIDL wire table type,
// passing in an externally managed table frame object view.
//
// Table members are set by passing |ObjectView|s into a builder method named
// for the member.
//
// To get the built table call |Build()|. The builder must not be used after
// |Build()| has been called.
template <typename FidlTable>
class WireTableExternalBuilder;

namespace internal {

// |WireTableBaseBuilder| holds the shared code between |WireTableBuilder| and
// |WireTableExternalBuilder|. It shouldn't be used directly.
template <typename FidlTable, typename Builder>
class WireTableBaseBuilder;

constexpr WireFormatVersion kLLCPPWireFormatVersion = WireFormatVersion::kV2;

// Marker to allow references/pointers to the unowned input objects in OwnedEncodedMessage.
// This enables iovec optimizations but requires the input objects to stay in scope until the
// encoded result has been consumed.
struct AllowUnownedInputRef {};

// This type exists because of class initialization order.
// If these are members of UnownedEncodedMessage, they will be initialized before
// UnownedEncodedMessageBase
template <typename FidlType, typename Transport>
struct UnownedEncodedMessageHandleContainer {
 protected:
  static constexpr uint32_t kNumHandles =
      fidl::internal::ClampedHandleCount<FidlType, fidl::MessageDirection::kSending>();
  std::array<zx_handle_t, kNumHandles> handle_storage_;
  std::array<typename Transport::HandleMetadata, kNumHandles> handle_metadata_storage_;
};

template <typename Transport>
class UnownedEncodedMessageBase {
 public:
  zx_status_t status() const { return message_.status(); }
#ifdef __Fuchsia__
  const char* status_string() const { return message_.status_string(); }
#endif
  bool ok() const { return message_.status() == ZX_OK; }
  std::string FormatDescription() const { return message_.FormatDescription(); }
  const char* lossy_description() const { return message_.lossy_description(); }
  const ::fidl::Status& error() const { return message_.error(); }

  ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_; }

  ::fidl::WireFormatMetadata wire_format_metadata() const {
    return fidl::internal::WireFormatMetadataForVersion(wire_format_version_);
  }

  template <typename TransportObject>
  void Write(TransportObject&& client, WriteOptions options = {}) {
    message_.Write(std::forward<TransportObject>(client), std::move(options));
  }

 protected:
  UnownedEncodedMessageBase(::fidl::internal::WireFormatVersion wire_format_version,
                            uint32_t iovec_capacity,
                            ::fit::result<::fidl::Error, ::fidl::BufferSpan> backing_buffer,
                            fidl_handle_t* handles, fidl_handle_metadata_t* handle_metadata,
                            uint32_t handle_capacity, bool is_transactional, void* value,
                            size_t inline_size, TopLevelEncodeFn encode_fn)
      : message_(likely(backing_buffer.is_ok())
                     ? ::fidl::OutgoingMessage::Create_InternalMayBreak(
                           ::fidl::OutgoingMessage::InternalIovecConstructorArgs{
                               .transport_vtable = &Transport::VTable,
                               .iovecs = iovecs_,
                               .iovec_capacity = iovec_capacity,
                               .handles = handles,
                               .handle_metadata = handle_metadata,
                               .handle_capacity = handle_capacity,
                               .backing_buffer = backing_buffer->data,
                               .backing_buffer_capacity = backing_buffer->capacity,
                               .is_transactional = is_transactional,
                           })
                     : ::fidl::OutgoingMessage{backing_buffer.error_value()}),
        wire_format_version_(wire_format_version) {
    if (likely(message_.ok())) {
      ZX_DEBUG_ASSERT(iovec_capacity <= std::size(iovecs_));
      message_.EncodeImpl(wire_format_version, value, inline_size, encode_fn);
    }
  }

  UnownedEncodedMessageBase(const UnownedEncodedMessageBase&) = delete;
  UnownedEncodedMessageBase(UnownedEncodedMessageBase&&) = delete;
  UnownedEncodedMessageBase* operator=(const UnownedEncodedMessageBase&) = delete;
  UnownedEncodedMessageBase* operator=(UnownedEncodedMessageBase&&) = delete;

 private:
  zx_channel_iovec_t iovecs_[Transport::kNumIovecs];
  fidl::OutgoingMessage message_;
  fidl::internal::WireFormatVersion wire_format_version_;
};

template <typename FidlType, typename Transport = internal::ChannelTransport>
class UnownedEncodedMessage final
    : public fidl::internal::UnownedEncodedMessageHandleContainer<FidlType, Transport>,
      public fidl::internal::UnownedEncodedMessageBase<Transport> {
  using UnownedEncodedMessageHandleContainer =
      fidl::internal::UnownedEncodedMessageHandleContainer<FidlType, Transport>;
  using UnownedEncodedMessageBase = ::fidl::internal::UnownedEncodedMessageBase<Transport>;

 public:
  UnownedEncodedMessage(uint8_t* backing_buffer, uint32_t backing_buffer_size, FidlType* response)
      : UnownedEncodedMessage(Transport::kNumIovecs, backing_buffer, backing_buffer_size,
                              response) {}
  UnownedEncodedMessage(fidl::internal::WireFormatVersion wire_format_version,
                        uint8_t* backing_buffer, uint32_t backing_buffer_size, FidlType* response)
      : UnownedEncodedMessage(wire_format_version, Transport::kNumIovecs, backing_buffer,
                              backing_buffer_size, response) {}
  UnownedEncodedMessage(uint32_t iovec_capacity, uint8_t* backing_buffer,
                        uint32_t backing_buffer_size, FidlType* response)
      : UnownedEncodedMessage(fidl::internal::kLLCPPWireFormatVersion, iovec_capacity,
                              backing_buffer, backing_buffer_size, response) {}

  // Encodes |value| by allocating a backing buffer from |backing_buffer_allocator|.
  UnownedEncodedMessage(fidl::internal::AnyBufferAllocator& backing_buffer_allocator,
                        uint32_t backing_buffer_size, FidlType* value)
      : UnownedEncodedMessage(::fidl::internal::kLLCPPWireFormatVersion, Transport::kNumIovecs,
                              backing_buffer_allocator.TryAllocate(backing_buffer_size), value) {}

  // Encodes |value| using an existing |backing_buffer|.
  UnownedEncodedMessage(fidl::internal::WireFormatVersion wire_format_version,
                        uint32_t iovec_capacity, uint8_t* backing_buffer,
                        uint32_t backing_buffer_size, FidlType* value)
      : UnownedEncodedMessage(wire_format_version, iovec_capacity,
                              ::fit::ok(::fidl::BufferSpan(backing_buffer, backing_buffer_size)),
                              value) {}

 private:
  // Core implementation which other constructors delegate to.
  UnownedEncodedMessage(::fidl::internal::WireFormatVersion wire_format_version,
                        uint32_t iovec_capacity,
                        ::fit::result<::fidl::Error, ::fidl::BufferSpan> backing_buffer,
                        FidlType* value)
      : UnownedEncodedMessageBase(
            wire_format_version, iovec_capacity, backing_buffer,
            UnownedEncodedMessageHandleContainer::handle_storage_.data(),
            reinterpret_cast<fidl_handle_metadata_t*>(
                UnownedEncodedMessageHandleContainer::handle_metadata_storage_.data()),
            UnownedEncodedMessageHandleContainer::kNumHandles,
            fidl::IsFidlTransactionalMessage<FidlType>::value, value,
            internal::TopLevelCodingTraits<FidlType>::inline_size,
            internal::MakeTopLevelEncodeFn<FidlType>()) {}

  UnownedEncodedMessage(const UnownedEncodedMessage&) = delete;
  UnownedEncodedMessage(UnownedEncodedMessage&&) = delete;
  UnownedEncodedMessage* operator=(const UnownedEncodedMessage&) = delete;
  UnownedEncodedMessage* operator=(UnownedEncodedMessage&&) = delete;
};

class EncodeResult {
 public:
  virtual ::fidl::OutgoingMessage& message() = 0;
  virtual ::fidl::WireFormatMetadata wire_format_metadata() const = 0;
};

// |AnyEncodeResult| can hold and own the encode result of either wire or natural
// domain objects. Its primary purpose is to share the same easy to use public
// encode APIs between wire and natural types, without necessarily incurring
// allocation.
//
// It reserves 2048 bytes of inline space. This is a conservative reservation to
// contain 64 handles and their metadata, plus 512 bytes of inline byte storage
// in the case of wire domain objects, and any miscellaneous bookkeeping. We
// would be able to shrink this by shifting large allocations to the heap
// without changing API.
using AnyEncodeResult = fit::pinned_inline_any<EncodeResult, /* Reserve */ 2048, /* Align */ 16>;

}  // namespace internal

// |OwnedEncodeResult| holds a message encoded for writing, along with the
// required storage.
//
// When holding encode results of wire domain objects, |OwnedEncodeResult|
// will try not to heap allocate when the encoded message size is small.
// For this reason, it is not moveable to prevent expensive byte copies.
class OwnedEncodeResult {
 public:
  // The message encoded for writing, or an error.
  //
  // Before using the message, one should first check it for encoding errors:
  //
  //     fidl::OwnedEncodeResult result = fidl::Encode(...);
  //     if (!result.message().ok()) {
  //       // Handle errors...
  //       fidl::Error error = result.message().error();
  //     }
  //
  // Note that as an optimization, handle types and rights are not validated.
  // Validation happens when writing to the transport. To proactively perform
  // validation, consult |OutgoingToIncomingMessage|.
  ::fidl::OutgoingMessage& message() { return result_->message(); }

  // The format and revision of the encoded FIDL message.
  ::fidl::WireFormatMetadata wire_format_metadata() const {
    return result_->wire_format_metadata();
  }

  template <typename T, typename... Args>
  explicit OwnedEncodeResult(cpp17::in_place_type_t<T> tag, Args&&... args)
      : result_(tag, std::forward<Args>(args)...) {}

 private:
  internal::AnyEncodeResult result_;
};

// Encodes an instance of |FidlType| for use over the Zircon channel transport.
// Supported types are structs, tables, and unions. |FidlType| should be a
// wire domain object.
//
// Handles in the current instance, if any, are moved to the returned
// |OwnedEncodeResult|.
//
// Errors during encoding (e.g. constraint validation) are reflected in the
// |message| of the returned |OwnedEncodeResult|.
//
// Example:
//
//     fuchsia_my_lib::wire::SomeType some_value = {...};
//     fidl::OwnedEncodeResult encoded = fidl::Encode(std::move(some_value));
//
//     if (!encoded.message().ok()) {
//       // Handle errors...
//     }
//
//     // Different ways to access the encoded payload:
//     // 1. View each iovec (output is always in vectorized chunks).
//     for (uint32_t i = 0; i < encoded.message().iovec_actual(); ++i) {
//       encoded.message().iovecs()[i].buffer;
//       encoded.message().iovecs()[i].capacity;
//     }
//
//     // 2. Copy the bytes to contiguous storage.
//     fidl::OutgoingMessage::CopiedBytes bytes = encoded.message().CopyBytes();
//
template <typename FidlType,
          size_t kEnabled = internal::TopLevelCodingTraits<FidlType>::inline_size>
OwnedEncodeResult Encode(FidlType& value) {
  static_assert(IsFidlType<FidlType>::value, "Only FIDL types are supported");

  class Encoded final : public internal::EncodeResult {
   public:
    explicit Encoded(FidlType* value)
        : message_(1u, backing_buffer_.data(), static_cast<uint32_t>(backing_buffer_.size()),
                   value) {}

    ::fidl::OutgoingMessage& message() override { return message_.GetOutgoingMessage(); }

    ::fidl::WireFormatMetadata wire_format_metadata() const override {
      return message_.wire_format_metadata();
    }

   private:
    ::fidl::internal::OutgoingMessageBuffer<FidlType> backing_buffer_;
    ::fidl::internal::UnownedEncodedMessage<FidlType, internal::ChannelTransport> message_;
  };

  return OwnedEncodeResult(cpp17::in_place_type_t<Encoded>{}, &value);
}

// |InplaceDecode| decodes the |message| to a wire domain
// object |FidlType|. Supported types are structs, tables, and unions.
// Bytes are mutated in-place. The bytes must remain alive if one needs to
// access the decoded result.
//
// Example:
//
//     // Create a message referencing an encoded payload.
//     fidl::EncodedMessage message = fidl::EncodedMessage::Create(byte_span);
//
//     // Decode the message.
//     fit::result decoded = fidl::InplaceDecode<fuchsia_my_lib::wire::SomeType>(
//         std::move(message), wire_format_metadata);
//
//     // Use the decoded value.
//     if (!decoded.is_ok()) {
//       // Handle errors...
//     }
//     fuchsia_my_lib::wire::SomeType& obj = *decoded.value();
//
// |message| is always consumed. |metadata| informs the wire format of the
// encoded message.
template <typename FidlType>
::fit::result<::fidl::Error, ::fidl::DecodedValue<FidlType>> InplaceDecode(
    EncodedMessage message, WireFormatMetadata metadata) {
  static_assert(IsFidlType<FidlType>::value, "Only FIDL types are supported");

  bool contains_envelope = TypeTraits<FidlType>::kHasEnvelope;
  size_t inline_size = internal::TopLevelCodingTraits<FidlType>::inline_size;
  const internal::TopLevelDecodeFn decode_fn = internal::MakeTopLevelDecodeFn<FidlType>();
  const Status status =
      internal::WireDecode(metadata, contains_envelope, inline_size, decode_fn, message);

  if (!status.ok()) {
    return ::fit::error(status);
  }
  return ::fit::ok(DecodedValue<FidlType>(reinterpret_cast<FidlType*>(message.bytes().data())));
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_TYPES_H_
