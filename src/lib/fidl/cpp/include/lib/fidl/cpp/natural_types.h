// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_

#include <lib/fidl/cpp/coding_traits.h>
#include <lib/fidl/cpp/internal/message_extensions.h>
#include <lib/fidl/cpp/internal/natural_types.h>
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

template <typename T>
struct CodingTraits<cpp17::optional<std::vector<T>>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_vector_t);

  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, cpp17::optional<std::vector<T>>* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    if (value->has_value()) {
      fidl::CodingTraits<std::vector<T>>::Encode(encoder, &value->value(), offset,
                                                 maybe_handle_info);
      return;
    }
    fidl_vector_t* vec = encoder->template GetPtr<fidl_vector_t>(offset);
    vec->count = 0;
    vec->data = reinterpret_cast<char*>(FIDL_ALLOC_ABSENT);
  }
  template <typename DecoderImpl>
  static void Decode(DecoderImpl* decoder, cpp17::optional<std::vector<T>>* value, size_t offset) {
    fidl_vector_t* vec = decoder->template GetPtr<fidl_vector_t>(offset);
    if (vec->data == nullptr) {
      ZX_ASSERT(vec->count == 0);
      value->reset();
      return;
    }
    std::vector<T> unwrapped;
    fidl::CodingTraits<std::vector<T>>::Decode(decoder, &unwrapped, offset);
    value->emplace(std::move(unwrapped));
  }
};

template <>
struct CodingTraits<cpp17::optional<std::string>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_string_t);

  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, cpp17::optional<std::string>* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(!maybe_handle_info.has_value());
    if (value->has_value()) {
      fidl::CodingTraits<std::string>::Encode(encoder, &value->value(), offset);
      return;
    }
    fidl_string_t* string = encoder->template GetPtr<fidl_string_t>(offset);
    string->size = 0;
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_ABSENT);
  }
  template <typename DecoderImpl>
  static void Decode(DecoderImpl* decoder, cpp17::optional<std::string>* value, size_t offset) {
    fidl_string_t* string = decoder->template GetPtr<fidl_string_t>(offset);
    if (string->data == nullptr) {
      ZX_ASSERT(string->size == 0);
      value->reset();
      return;
    }
    std::string unwrapped;
    fidl::CodingTraits<std::string>::Decode(decoder, &unwrapped, offset);
    value->emplace(unwrapped);
  }
};

template <typename T>
struct CodingTraits<std::unique_ptr<T>, typename std::enable_if<IsUnion<T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_xunion_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_xunion_v2_t);

  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::unique_ptr<T>* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    if (*value) {
      CodingTraits<T>::Encode(encoder, value->get(), offset, maybe_handle_info);
      return;
    }

    // Buffer is zero-initialized.
  }

  template <typename DecoderImpl>
  static void Decode(DecoderImpl* decoder, std::unique_ptr<T>* value, size_t offset) {
    fidl_xunion_v2_t* u = decoder->template GetPtr<fidl_xunion_v2_t>(offset);
    if (FidlIsZeroEnvelope(&u->envelope)) {
      *value = nullptr;
      return;
    }
    *value = std::make_unique<T>();
    CodingTraits<T>::Decode(decoder, value->get(), offset);
  }
};

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
  const T* operator->() const& { return std::get_if<I>(storage.get()); }
  T* operator->() & { return std::get_if<I>(storage.get()); }

  template <class U>
  constexpr T value_or(U&& default_value) const& {
    if (storage->index() == I) {
      return value();
    }
    return default_value;
  }
  // TODO: non-const value_or

  // TODO: comparison operators? emplace & swap?

  // Move into a std::optional, invalidating the union.
  std::optional<T> take() && noexcept {
    V v{};
    storage->swap(v);
    if (v.index() == I) {
      return std::make_optional(std::move(std::get<I>(v)));
    }
    return std::nullopt;
  }

  // Copy into an std::optional.
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
  EncodeResult(const fidl_type_t* type, ::fidl::BodyEncoder&& storage)
      : storage_(std::move(storage)),
        message_(ConvertFromHLCPPOutgoingBody(storage.wire_format(), type, storage_.GetBody(),
                                              handles_, handle_metadata_)) {}

  ::fidl::OutgoingMessage& message() { return message_; }

 private:
  zx_handle_t handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t handle_metadata_[ZX_CHANNEL_MAX_MSG_HANDLES];
  ::fidl::BodyEncoder storage_;
  ::fidl::OutgoingMessage message_;
};

// |DecodeFrom| decodes a non-transactional incoming message to a natural
// domain object |FidlType|. Supported types are structs, tables, and unions.
//
// |message| is always consumed.
// |metadata| informs the wire format of the encoded message.
template <typename FidlType>
::fitx::result<::fidl::Error, FidlType> DecodeFrom(::fidl::IncomingMessage&& message,
                                                   ::fidl::internal::WireFormatMetadata metadata) {
  static_assert(::fidl::IsFidlType<FidlType>::value, "Only FIDL types are supported");
  ZX_ASSERT(!message.is_transactional());

  const fidl_type_t* coding_table = TypeTraits<FidlType>::kCodingTable;
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  ::fidl::HLCPPIncomingBody hlcpp_body = ConvertToHLCPPIncomingBody(std::move(message), handles);
  const char* error_msg = nullptr;
  zx_status_t status = hlcpp_body.Decode(metadata, coding_table, &error_msg);
  if (status != ZX_OK) {
    return ::fitx::error(::fidl::Result::DecodeError(status, error_msg));
  }
  ::fidl::Decoder decoder{std::move(hlcpp_body)};
  FidlType value{};
  ::fidl::CodingTraits<FidlType>::Decode(&decoder, &value, 0);
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
template <typename FidlType>
::fidl::internal::EncodeResult EncodeIntoResult(FidlType& value) {
  static_assert(::fidl::IsFidlType<FidlType>::value, "Only FIDL types are supported");
  const fidl_type_t* coding_table = TypeTraits<FidlType>::kCodingTable;
  // Since a majority of the domain objects are HLCPP objects, for now
  // the wire format version of the encoded message is the same as the one
  // used in HLCPP.
  ::fidl::BodyEncoder encoder(DefaultHLCPPEncoderWireFormat());
  encoder.Alloc(::fidl::EncodingInlineSize<FidlType, ::fidl::Encoder>(&encoder));
  ::fidl::CodingTraits<FidlType>::Encode(&encoder, &value, 0);
  return EncodeResult(coding_table, std::move(encoder));
}

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_
