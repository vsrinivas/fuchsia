// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/internal/natural_types.h>
#include <lib/fidl/cpp/natural_coding_traits.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fidl/cpp/wire/wire_types.h>
#include <lib/fidl/cpp/wire_format_metadata.h>
#include <lib/fidl/cpp/wire_natural_conversions.h>
#include <lib/stdcompat/optional.h>

#include <cstdint>

#include "lib/stdcompat/utility.h"

// # Natural domain objects
//
// This header contains forward definitions that are part of natural domain
// objects. The code generator should populate the implementation by generating
// template specializations for each FIDL data type.
namespace fidl {

class Decoder;

namespace internal {

// |UnionMemberView| is a helper class for union members.
// It's returned by various accessor methods on union natural domain objects.
// It holds a shared_ptr reference to the underlying variant of the union.

template <size_t I, typename V>
class UnionMemberView final {
 private:
  using Storage = std::shared_ptr<V>;
  Storage storage{};
  using T = std::variant_alternative_t<I, V>;

 public:
  explicit UnionMemberView(Storage storage) : storage(storage) {}

  UnionMemberView& operator=(const T& value) {
    storage->emplace<I>(value);
    return *this;
  }

  UnionMemberView& operator=(T&& value) {
    storage->template emplace<I>(std::move(value));
    return *this;
  }

  // A std::optional-like API:
  explicit operator bool() const noexcept { return has_value(); }
  bool has_value() const noexcept { return storage->index() == I; }

  const T& value() const& {
    const V& variant = *storage;
    return std::get<I>(variant);
  }
  T& value() & {
    V& variant = *storage;
    return std::get<I>(variant);
  }
  T& value() && { return value(); }

  const T* operator->() const& { return std::get_if<I>(storage.get()); }
  T* operator->() & { return std::get_if<I>(storage.get()); }
  T* operator->() && { return operator->(); }

  template <class U>
  constexpr T value_or(U&& default_value) const& {
    if (storage->index() == I) {
      return value();
    }
    return default_value;
  }
  // TODO: non-const value_or

  // TODO: comparison operators? emplace & swap?

  // Move into a std::optional.
  // The union holds the same field with a moved-from state.
  std::optional<T> take() && noexcept {
    if (storage->index() == I) {
      return std::make_optional(std::move(std::get<I>(*storage)));
    }
    return std::nullopt;
  }

  // Copy into an std::optional.
  // The union holds the same field whose content is unchanged.
  template <typename U = T, typename = std::enable_if_t<std::is_copy_constructible<U>::value>>
  operator std::optional<T>() const& noexcept {
    if (storage->index() == I) {
      T value = std::get<I>(*storage);
      return std::make_optional(value);
    }
    return std::nullopt;
  }
};

// Use it like `template <typename T, EnableIfNaturalType<T> = nullptr>` to enable the
// declaration only for natural types.
template <typename FidlType>
using EnableIfNaturalType = std::enable_if_t<static_cast<bool>(
    internal::NaturalCodingTraits<FidlType,
                                  internal::NaturalCodingConstraintEmpty>::inline_size_v2)>*;

class NaturalEncodeResult final : public EncodeResult {
 public:
  template <typename F>
  explicit NaturalEncodeResult(const TransportVTable* vtable,
                               internal::WireFormatVersion wire_format, F encode_callback)
      : storage_(vtable, wire_format), message_([&]() {
          encode_callback(storage_);
          return storage_.GetOutgoingMessage(NaturalBodyEncoder::MessageType::kStandalone);
        }()) {}

  ::fidl::OutgoingMessage& message() override { return message_; }

  ::fidl::WireFormatMetadata wire_format_metadata() const override {
    return storage_.wire_format_metadata();
  }

 private:
  ::fidl::internal::NaturalBodyEncoder storage_;
  ::fidl::OutgoingMessage message_;
};

template <typename Transport, typename FidlType>
OwnedEncodeResult EncodeWithTransport(FidlType&& value) {
  static_assert(::fidl::IsFidlType<FidlType>::value, "Only FIDL types are supported");
  return OwnedEncodeResult(
      cpp17::in_place_type_t<NaturalEncodeResult>{}, &Transport::VTable,
      fidl::internal::WireFormatVersion::kV2,
      [value_ptr = &value](::fidl::internal::NaturalBodyEncoder& encoder) mutable {
        encoder.Alloc(
            ::fidl::internal::NaturalEncodingInlineSize<FidlType, NaturalCodingConstraintEmpty>(
                &encoder));
        ::fidl::internal::NaturalCodingTraits<FidlType, NaturalCodingConstraintEmpty>::Encode(
            &encoder, value_ptr, 0, kRecursionDepthInitial);
      });
}

fit::result<fidl::Error, std::tuple<fidl::WireFormatMetadata, std::vector<uint8_t>>>
OwnedSplitMetadataAndMessage(cpp20::span<const uint8_t> persisted);

}  // namespace internal

// Encodes an instance of |FidlType| for use over the Zircon channel transport.
// Supported types are structs, tables, and unions. |FidlType| should be a
// natural domain object.
//
// Handles in the current instance are moved to the returned
// |OwnedEncodeResult|, if any.
//
// Errors during encoding (e.g. constraint validation) are reflected in the
// |message| of the returned |OwnedEncodeResult|.
//
// Example:
//
//     fuchsia_my_lib::SomeType some_value = {...};
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
template <typename FidlType, internal::EnableIfNaturalType<FidlType> = nullptr>
OwnedEncodeResult Encode(FidlType value) {
  return internal::EncodeWithTransport<fidl::internal::ChannelTransport>(std::move(value));
}

// |Decode| decodes a non-transactional incoming message to a natural domain
// object |FidlType|. Supported types are structs, tables, and unions. Example:
//
//     // Create a message referencing an encoded payload.
//     fidl::EncodedMessage message = fidl::EncodedMessage::Create(byte_span);
//
//     // Decode the message.
//     fit::result decoded = fidl::Decode<fuchsia_my_lib::SomeType>(
//         std::move(message), wire_format_metadata);
//
//     // Use the decoded value.
//     if (!decoded.is_ok()) {
//       // Handle errors...
//     }
//     fuchsia_my_lib::SomeType& value = decoded.value();
//
// |message| is always consumed. |metadata| informs the wire format of the
// encoded message.
template <typename FidlType>
::fit::result<::fidl::Error, FidlType> Decode(::fidl::EncodedMessage message,
                                              ::fidl::WireFormatMetadata metadata) {
  static_assert(::fidl::IsFidlType<FidlType>::value, "Only FIDL types are supported");
  FidlType value{internal::DefaultConstructPossiblyInvalidObjectTag{}};

  bool contains_envelope = TypeTraits<FidlType>::kHasEnvelope;
  const size_t inline_size =
      internal::NaturalCodingTraits<FidlType,
                                    internal::NaturalCodingConstraintEmpty>::inline_size_v2;
  const internal::NaturalTopLevelDecodeFn decode_fn =
      internal::MakeNaturalTopLevelDecodeFn<FidlType>();
  const Status status = internal::NaturalDecode(metadata, contains_envelope, inline_size, decode_fn,
                                                message, static_cast<void*>(&value));

  if (!status.ok()) {
    return ::fit::error(status);
  }
  return ::fit::ok(std::move(value));
}

// |Persist| encodes a natural domain object |FidlType| into bytes, following
// the [convention for FIDL data persistence][persistence-convention]: the
// wire format metadata followed by the encoded bytes. |FidlType| needs to
// satisfy these requirements:
//
// - |FidlType| is a natural struct/union/table.
// - |FidlType| is not a resource type.
//
// Example:
//
//     fuchsia_my_lib::SomeType obj = ...;
//     fit::result result = fidl::Persist(obj);
//     if (result.is_error()) {
//       // Handle errors...
//     }
//     // Get the persisted data.
//     std::vector<uint8_t>& data = result.value();
//
// [persistence-convention]:
// https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0120_standalone_use_of_fidl_wire_format?hl=en#convention_for_data_persistence
template <typename FidlType, internal::EnableIfNaturalType<FidlType> = nullptr>
fit::result<fidl::Error, std::vector<uint8_t>> Persist(const FidlType& value) {
  static_assert(fidl::IsFidlType<FidlType>::value, "|FidlType| must be a FIDL domain object.");
  static_assert(
      !fidl::IsResource<FidlType>::value,
      "|FidlType| cannot be a resource type. Resources cannot be persisted. "
      "If you need to send resource types to another process, consider using a FIDL protocol.");

  fidl::OwnedEncodeResult encoded = fidl::Encode(value);
  if (!encoded.message().ok()) {
    return fit::error(encoded.message().error());
  }
  return fit::ok(
      internal::ConcatMetadataAndMessage(encoded.wire_format_metadata(), encoded.message()));
}

// |Unpersist| reads a const sequence of bytes stored in the
// [convention for FIDL data persistence][persistence-convention] into an
// instance of |FidlType|. |FidlType| needs to satisfy these requirements:
//
// - |FidlType| is a natural struct/union/table.
// - |FidlType| is not a resource type.
//
// Example:
//
//     const std::vector<uint8_t> data = ...;
//     fit::result result = fidl::Unpersist<fuchsia_my_lib::SomeType>(cpp20::span(data));
//     if (result.is_error()) {
//       // Handle errors...
//     }
//     // Get the decoded object.
//     fuchsia_my_lib::SomeType& obj = result.value();
//
// [persistence-convention]:
// https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0120_standalone_use_of_fidl_wire_format?hl=en#convention_for_data_persistence
template <typename FidlType>
fit::result<fidl::Error, FidlType> Unpersist(cpp20::span<const uint8_t> data) {
  static_assert(fidl::IsFidlType<FidlType>::value, "|FidlType| must be a FIDL domain object.");
  static_assert(
      !fidl::IsResource<FidlType>::value,
      "|FidlType| cannot be a resource type. Resources cannot be persisted. "
      "If you need to send resource types to another process, consider using a FIDL protocol.");

  fit::result split = internal::OwnedSplitMetadataAndMessage(data);
  if (split.is_error()) {
    return split.take_error();
  }
  auto [metadata, bytes] = split.value();
  return fidl::Decode<FidlType>(fidl::EncodedMessage::Create(bytes), metadata);
}

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_
