// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_TYPES_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_TYPES_H_

#include <lib/fidl/cpp/wire/decoded_value.h>
#include <lib/fidl/cpp/wire/envelope.h>
#include <lib/fidl/cpp/wire/incoming_message.h>
#include <lib/fidl/cpp/wire/object_view.h>
#include <lib/fidl/cpp/wire/wire_coding_traits.h>
#include <lib/fidl/cpp/wire_format_metadata.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

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

// |kNullCodingConfig| may be used when an incoming message is all bytes and
// does not have any handle.
extern const CodingConfig kNullCodingConfig;

}  // namespace internal

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
//     fitx::result decoded = fidl::InplaceDecode<fuchsia_my_lib::wire::SomeType>(
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
::fitx::result<::fidl::Error, ::fidl::DecodedValue<FidlType>> InplaceDecode(
    EncodedMessage message, WireFormatMetadata metadata) {
  using internal::kCodingErrorDoesNotSupportV1Envelopes;
  using internal::kCodingErrorInvalidWireFormatMetadata;
  using internal::kCodingErrorUnsupportedWireFormatVersion;

  static_assert(IsFidlType<FidlType>::value, "Only FIDL types are supported");

  if (!metadata.is_valid()) {
    return ::fitx::error(
        Status::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorInvalidWireFormatMetadata));
  }

  bool contains_envelope = TypeTraits<FidlType>::kHasEnvelope;
  // Old versions of the C bindings will send wire format V1 payloads that are compatible
  // with wire format V2 (they don't contain envelopes). Confirm that V1 payloads don't
  // contain envelopes and are compatible with V2.
  // TODO(fxbug.dev/99738): Remove this logic.
  if (contains_envelope &&
      metadata.wire_format_version() == fidl::internal::WireFormatVersion::kV1) {
    return ::fitx::error(
        Status::DecodeError(ZX_ERR_INVALID_ARGS, kCodingErrorDoesNotSupportV1Envelopes));
  }
  // TODO(fxbug.dev/99738): Drop "non-envelope V1" support.
  if (metadata.wire_format_version() != fidl::internal::WireFormatVersion::kV1 &&
      metadata.wire_format_version() != fidl::internal::WireFormatVersion::kV2) {
    return ::fitx::error(
        Status::DecodeError(ZX_ERR_NOT_SUPPORTED, kCodingErrorUnsupportedWireFormatVersion));
  }

  size_t inline_size = internal::TopLevelCodingTraits<FidlType>::inline_size;
  const internal::TopLevelDecodeFn decode_fn = internal::MakeTopLevelDecodeFn<FidlType>();
  const internal::CodingConfig* coding_config =
      message.transport_vtable() ? message.transport_vtable()->encoding_configuration
                                 : &internal::kNullCodingConfig;

  const Status status = internal::WireDecode(
      inline_size, decode_fn, coding_config, message.bytes().data(), message.bytes().size(),
      message.handles(), message.raw_handle_metadata(), message.handle_actual());
  std::move(message).ReleaseHandles();

  if (!status.ok()) {
    return ::fitx::error(status);
  }
  return ::fitx::ok(DecodedValue<FidlType>(reinterpret_cast<FidlType*>(message.bytes().data())));
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_TYPES_H_
