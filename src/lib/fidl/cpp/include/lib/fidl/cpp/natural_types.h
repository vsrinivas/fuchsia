// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/internal/natural_types.h>
#include <lib/fidl/cpp/natural_coding_traits.h>
#include <lib/fidl/cpp/wire_natural_conversions.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/wire_types.h>
#include <lib/stdcompat/optional.h>

#include <cstdint>

// # Natural domain objects
//
// This header contains forward definitions that are part of natural domain
// objects. The code generator should populate the implementation by generating
// template specializations for each FIDL data type.
namespace fidl {

class Decoder;

namespace internal {

// |TypeTraits| contains information about a natural domain object:
//
// - fidl_type_t* kCodingTable: pointer to the coding table.
//
template <typename FidlType>
struct TypeTraits;

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

// |EncodeResult| holds an encoded message along with the required storage.
// Success/failure information is stored in |message|.
class EncodeResult {
 public:
  explicit EncodeResult(::fidl::internal::NaturalBodyEncoder&& storage)
      : storage_(std::move(storage)), message_(std::move(storage_).GetBody()) {}

  ::fidl::OutgoingMessage& message() { return message_; }

 private:
  ::fidl::internal::NaturalBodyEncoder storage_;
  ::fidl::OutgoingMessage message_;
};

// |DecodeFrom| decodes a non-transactional incoming message to a natural
// domain object |FidlType|. Supported types are structs, tables, and unions.
//
// |message| is always consumed.
// |metadata| informs the wire format of the encoded message.
//
// TODO(fxbug.dev/82681): Make this API comply with the requirements in FIDL-at-rest.
template <typename FidlType>
::fitx::result<::fidl::Error, FidlType> DecodeFrom(::fidl::IncomingMessage&& message,
                                                   ::fidl::internal::WireFormatMetadata metadata) {
  static_assert(::fidl::IsFidlType<FidlType>::value, "Only FIDL types are supported");
  ZX_ASSERT(!message.is_transactional());

  uint32_t message_byte_actual = message.byte_actual();
  uint32_t message_handle_actual = message.handle_actual();
  ::fidl::internal::NaturalDecoder decoder(std::move(message), metadata.wire_format_version());
  size_t offset;
  if (!decoder.Alloc(
          ::fidl::internal::NaturalDecodingInlineSize<FidlType, NaturalCodingConstraintEmpty>(
              &decoder),
          &offset)) {
    return ::fitx::error(::fidl::Error::DecodeError(decoder.status(), decoder.error()));
  }

  FidlType value{DefaultConstructPossiblyInvalidObjectTag{}};
  ::fidl::internal::NaturalCodingTraits<FidlType, NaturalCodingConstraintEmpty>::Decode(
      &decoder, &value, offset, kRecursionDepthInitial);
  if (decoder.status() != ZX_OK) {
    return ::fitx::error(::fidl::Error::DecodeError(decoder.status(), decoder.error()));
  }
  if (decoder.CurrentLength() != message_byte_actual) {
    return ::fitx::error(
        ::fidl::Error::DecodeError(ZX_ERR_INTERNAL, kCodingErrorNotAllBytesConsumed));
  }
  if (decoder.CurrentHandleCount() != message_handle_actual) {
    return ::fitx::error(
        ::fidl::Error::DecodeError(ZX_ERR_INTERNAL, kCodingErrorNotAllHandlesConsumed));
  }
  return ::fitx::ok(std::move(value));
}

// Encodes an instance of |FidlType|. Supported types are structs, tables, and
// unions.
//
// Handles in the current instance are moved to the returned |EncodeResult|,
// if any.
//
// Errors during encoding (e.g. constraint validation) are reflected in the
// |message| of the returned |EncodeResult|.
//
// TODO(fxbug.dev/82681): Make this API comply with the requirements in FIDL-at-rest.
template <typename Transport, typename FidlType>
::fidl::internal::EncodeResult EncodeIntoResult(FidlType& value) {
  static_assert(::fidl::IsFidlType<FidlType>::value, "Only FIDL types are supported");
  ::fidl::internal::NaturalBodyEncoder encoder(&Transport::VTable,
                                               fidl::internal::WireFormatVersion::kV2);
  encoder.Alloc(::fidl::internal::NaturalEncodingInlineSize<FidlType, NaturalCodingConstraintEmpty>(
      &encoder));
  ::fidl::internal::NaturalCodingTraits<FidlType, NaturalCodingConstraintEmpty>::Encode(
      &encoder, &value, 0, kRecursionDepthInitial);
  return EncodeResult(std::move(encoder));
}

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_
