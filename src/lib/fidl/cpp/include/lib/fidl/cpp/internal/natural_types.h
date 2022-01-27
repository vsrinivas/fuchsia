// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_

#include <lib/fidl/cpp/coding_traits.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/wire_types.h>
#include <lib/fitx/result.h>
#include <zircon/fidl.h>

#include <cstdint>
#include <utility>
#include <variant>

// # Natural domain objects
//
// This header contains forward definitions that are part of natural domain
// objects. The code generator should populate the implementation by generating
// template specializations for each FIDL data type.
namespace fidl {

// |Error| is a type alias for when the result of an operation is an error.
using Error = Result;

namespace internal {

template <typename F>
void NaturalEnvelopeEncode(::fidl::Encoder* encoder, F* value, size_t offset,
                           std::optional<fidl::HandleInformation> handle_info) {
  if (value == nullptr) {
    // Nothing to encode.
    return;
  }

  const size_t length_before = encoder->CurrentLength();
  const size_t handles_before = encoder->CurrentHandleCount();
  switch (encoder->wire_format()) {
    case ::fidl::internal::WireFormatVersion::kV1: {
      ::fidl::Encode(encoder, value,
                     encoder->Alloc(::fidl::EncodingInlineSize<F, ::fidl::Encoder>(encoder)),
                     handle_info);

      // Call GetPtr after Encode because the buffer may move.
      fidl_envelope_t* envelope = encoder->GetPtr<fidl_envelope_t>(offset);
      envelope->num_bytes = static_cast<uint32_t>(encoder->CurrentLength() - length_before);
      envelope->num_handles = static_cast<uint32_t>(encoder->CurrentHandleCount() - handles_before);
      envelope->presence = FIDL_ALLOC_PRESENT;
      break;
    }
    case ::fidl::internal::WireFormatVersion::kV2: {
      if (::fidl::EncodingInlineSize<F>(encoder) <= FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
        ::fidl::Encode(encoder, value, offset, handle_info);

        // Call GetPtr after Encode because the buffer may move.
        fidl_envelope_v2_t* envelope = encoder->GetPtr<fidl_envelope_v2_t>(offset);
        envelope->num_handles =
            static_cast<uint16_t>(encoder->CurrentHandleCount() - handles_before);
        envelope->flags = FIDL_ENVELOPE_FLAGS_INLINING_MASK;
        break;
      }

      ::fidl::Encode(encoder, value,
                     encoder->Alloc(::fidl::EncodingInlineSize<F, ::fidl::Encoder>(encoder)),
                     handle_info);

      // Call GetPtr after Encode because the buffer may move.
      fidl_envelope_v2_t* envelope = encoder->GetPtr<fidl_envelope_v2_t>(offset);
      envelope->num_bytes = static_cast<uint32_t>(encoder->CurrentLength() - length_before);
      envelope->num_handles = static_cast<uint16_t>(encoder->CurrentHandleCount() - handles_before);
      envelope->flags = 0;
      break;
    }
  }
}

template <typename F>
void NaturalEnvelopeEncodeOptional(::fidl::Encoder* encoder, std::optional<F>* value, size_t offset,
                                   std::optional<fidl::HandleInformation> handle_info) {
  if (!value->has_value()) {
    // Nothing to encode.
    return;
  }
  NaturalEnvelopeEncode(encoder, &value->value(), offset, handle_info);
}

template <typename F>
void NaturalEnvelopeDecodeOptional(::fidl::Decoder* decoder, std::optional<F>* value,
                                   size_t offset) {
  fidl_envelope_v2_t* envelope = decoder->GetPtr<fidl_envelope_v2_t>(offset);
  if (*reinterpret_cast<const void* const*>(envelope) != nullptr) {
    value->emplace();
    ::fidl::Decode(decoder, &value->value(), decoder->EnvelopeValueOffset(envelope));
  } else {
    value->reset();
  }
}

template <typename F>
bool NaturalEnvelopeDecode(::fidl::Decoder* decoder, F* value, size_t offset) {
  fidl_envelope_v2_t* envelope = decoder->GetPtr<fidl_envelope_v2_t>(offset);
  if (*reinterpret_cast<const void* const*>(envelope) != nullptr) {
    ::fidl::Decode(decoder, value, decoder->EnvelopeValueOffset(envelope));
    return true;
  } else {
    return false;
  }
}

// This holds metadata about a struct member: a member pointer to the member's value in
// the struct's Storage_ type, offsets, and optionally handle information.
template <typename T, typename F>
struct NaturalStructMember final {
  F T::*member_ptr;
  size_t offset_v1;
  size_t offset_v2;
  std::optional<fidl::HandleInformation> handle_info;
  explicit constexpr NaturalStructMember(
      F T::*member_ptr, size_t offset_v1, size_t offset_v2,
      std::optional<fidl::HandleInformation> handle_info = std::nullopt) noexcept
      : member_ptr(member_ptr),
        offset_v1(offset_v1),
        offset_v2(offset_v2),
        handle_info(handle_info) {}
};

template <typename T, size_t V1, size_t V2>
struct NaturalStructCodingTraits {
  static constexpr size_t inline_size_v1_no_ee = V1;
  static constexpr size_t inline_size_v2 = V2;

  // Visit each of the members of the struct in order.
  template <typename F, size_t I = 0>
  static void Visit(T* table, F func) {
    if constexpr (I < std::tuple_size_v<decltype(T::kMembers)>) {
      auto member_info = std::get<I>(T::kMembers);
      auto* member_ptr = &(table->storage_.*(member_info.member_ptr));
      func(member_ptr, member_info);
      Visit<F, I + 1>(table, func);
    }
  }

  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, T* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    const auto wire_format = encoder->wire_format();
    Visit(value, [&](auto* member, auto& member_info) {
      size_t field_offset = wire_format == ::fidl::internal::WireFormatVersion::kV1
                                ? member_info.offset_v1
                                : member_info.offset_v2;
      ::fidl::Encode(encoder, member, offset + field_offset, member_info.handle_info);
    });
  }

  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, T* value, size_t offset) {
    Visit(value, [&](auto* member, auto& member_info) {
      ::fidl::Decode(decoder, member, offset + member_info.offset_v2);
    });
  }
};

// This holds metadata about a table member: its ordinal, a member pointer to the member's value in
// the table's Storage_ type, and optionally handle information.
template <typename T, typename F>
struct NaturalTableMember final {
  size_t ordinal;
  std::optional<F> T::*member_ptr;
  std::optional<fidl::HandleInformation> handle_info;
  constexpr NaturalTableMember(
      size_t ordinal, std::optional<F> T::*member_ptr,
      std::optional<fidl::HandleInformation> handle_info = std::nullopt) noexcept
      : ordinal(ordinal), member_ptr(member_ptr), handle_info(handle_info) {}
};

template <typename T>
struct NaturalTableCodingTraits {
  static constexpr size_t inline_size_v1_no_ee = 16;
  static constexpr size_t inline_size_v2 = 16;

  // Visit each of the members of the table in order.
  template <typename F, size_t I = 0>
  static void Visit(T* table, F func) {
    if constexpr (I < std::tuple_size_v<decltype(T::kMembers)>) {
      auto member_info = std::get<I>(T::kMembers);
      auto* member_ptr = &(table->storage_.*(member_info.member_ptr));
      func(member_ptr, member_info);
      Visit<F, I + 1>(table, func);
    }
  }

  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, T* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    size_t max_ordinal = MaxOrdinal(value);
    ::fidl::EncodeVectorPointer(encoder, max_ordinal, offset);
    if (max_ordinal == 0)
      return;
    size_t envelope_size = (encoder->wire_format() == ::fidl::internal::WireFormatVersion::kV1)
                               ? sizeof(fidl_envelope_t)
                               : sizeof(fidl_envelope_v2_t);
    size_t base = encoder->Alloc(max_ordinal * envelope_size);
    Visit(value, [&](auto* member, auto& member_info) {
      size_t offset = base + (member_info.ordinal - 1) * envelope_size;
      NaturalEnvelopeEncodeOptional(encoder, member, offset, member_info.handle_info);
    });
  }

  // Returns the largest ordinal of a present table member.
  template <size_t I = std::tuple_size_v<decltype(T::kMembers)> - 1>
  static size_t MaxOrdinal(T* value) {
    if constexpr (I == -1) {
      return 0;
    } else {
      auto T::Storage_::*member_ptr = std::get<I>(T::kMembers).member_ptr;
      const auto& member = value->storage_.*member_ptr;
      if (member.has_value()) {
        return I + 1;
      }
      return MaxOrdinal<I - 1>(value);
    }
  }

  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, T* value, size_t offset) {
    fidl_vector_t* encoded = decoder->template GetPtr<fidl_vector_t>(offset);

    if (!encoded->data) {
      *value = T{};
      return;
    }

    size_t base = decoder->GetOffset(encoded->data);
    size_t count = encoded->count;
    constexpr size_t envelope_size = sizeof(fidl_envelope_v2_t);

    Visit(value, [&](auto* member, auto& member_info) {
      size_t member_offset = base + (member_info.ordinal - 1) * envelope_size;
      if (member_info.ordinal <= count) {
        NaturalEnvelopeDecodeOptional(decoder, member, member_offset);
      } else {
        member->reset();
      }
    });
  }
};

template <typename T>
struct NaturalUnionCodingTraits {
  static constexpr size_t inline_size_v1_no_ee = 24;
  static constexpr size_t inline_size_v2 = 16;

  static void Encode(Encoder* encoder, T* value, size_t offset,
                     cpp17::optional<::fidl::HandleInformation> maybe_handle_info) {
    const size_t index = value->storage_->index();
    ZX_ASSERT(index > 0);
    const size_t envelope_offset = offset + offsetof(fidl_xunion_t, envelope);
    std::visit(
        [&](auto& member_value) {
          if constexpr (!std::is_same_v<decltype(member_value), std::monostate&>) {
            NaturalEnvelopeEncode(encoder, &member_value, envelope_offset, T::kMembers[index]);
          }
        },
        *value->storage_);
    // Call GetPtr after Encode because the buffer may move.
    fidl_xunion_v2_t* xunion = encoder->GetPtr<fidl_xunion_v2_t>(offset);
    xunion->tag = static_cast<fidl_union_tag_t>(T::IndexToTag(index));
  }

  static void Decode(Decoder* decoder, T* value, size_t offset) {
    fidl_xunion_v2_t* xunion = decoder->GetPtr<fidl_xunion_v2_t>(offset);
    const size_t index = T::TagToIndex(static_cast<typename T::Tag>(xunion->tag));
    const size_t envelope_offset = offset + offsetof(fidl_xunion_t, envelope);
    if (index > 0) {
      DecodeMember(decoder, value, envelope_offset, index);
    } else {
      *value = T();
      // TODO: do I need to skip the envelope contents somehow here?
    }
  }

  template <size_t I = 1>
  static void DecodeMember(Decoder* decoder, T* value, size_t envelope_offset, const size_t index) {
    static_assert(I > 0);
    if constexpr (I < std::variant_size_v<typename T::Storage_>) {
      if (I == index) {
        value->storage_->template emplace<I>();
        NaturalEnvelopeDecode(decoder, &std::get<I>(*value->storage_), envelope_offset);
        return;
      }
      return DecodeMember<I + 1>(decoder, value, envelope_offset, index);
    }
    // TODO: dcheck
  }
};

// Helpers for deep-copying some types that aren't copy-constructible.
// In particular ones that use std::unique_ptr, a common pattern in natural domain objects.
template <typename T>
struct NaturalCloneHelper {
  static T Clone(const T& value) { return value; }
};

template <typename T>
struct NaturalCloneHelper<std::optional<T>> {
  static std::optional<T> Clone(const std::optional<T>& value) {
    if (value) {
      return std::make_optional<T>(NaturalCloneHelper<T>::Clone(value.value()));
    }
    return std::nullopt;
  }
};

template <typename T>
struct NaturalCloneHelper<std::unique_ptr<T>> {
  static std::unique_ptr<T> Clone(const std::unique_ptr<T>& value) {
    return std::make_unique<T>(*value.get());
  }
};

template <typename T>
struct NaturalCloneHelper<std::vector<T>> {
  static std::vector<T> Clone(const std::vector<T>& value) {
    std::vector<T> clone{};
    clone.reserve(value.size());
    std::transform(value.begin(), value.end(), std::back_inserter(clone),
                   [](const T& v) { return NaturalCloneHelper<T>::Clone(v); });
    return clone;
  }
};

template <typename T, size_t N, std::size_t... Indexes>
std::array<T, N> ArrayCloneHelper(const std::array<T, N>& value, std::index_sequence<Indexes...>) {
  return std::array<T, N>{NaturalCloneHelper<T>::Clone(std::get<Indexes>(value))...};
}

template <typename T, size_t N>
struct NaturalCloneHelper<std::array<T, N>> {
  static std::array<T, N> Clone(const std::array<T, N>& value) {
    return ArrayCloneHelper(value, std::make_index_sequence<N>());
  }
};

template <typename T>
T NaturalClone(const T& value) {
  return NaturalCloneHelper<T>::Clone(value);
}

}  // namespace internal

template <>
struct CodingTraits<::std::string> final {
  static constexpr size_t inline_size_old = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_string_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::string* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(!maybe_handle_info);
    const size_t size = value->size();
    fidl_string_t* string = encoder->template GetPtr<fidl_string_t>(offset);
    string->size = size;
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    size_t base = encoder->Alloc(size);
    char* payload = encoder->template GetPtr<char>(base);
    memcpy(payload, value->data(), size);
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, std::string* value, size_t offset) {
    fidl_string_t* string = decoder->template GetPtr<fidl_string_t>(offset);
    ZX_ASSERT(string->data != nullptr);
    *value = std::string(string->data, string->size);
  }
};

template <typename T>
struct CodingTraits<ClientEnd<T>> {
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, ClientEnd<T>* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(maybe_handle_info);
    zx::channel channel = value->TakeChannel();
    encoder->EncodeHandle(&channel, maybe_handle_info->object_type, maybe_handle_info->rights,
                          offset);
  }

  static void Decode(Decoder* decoder, ClientEnd<T>* value, size_t offset) {
    zx::channel channel;
    decoder->DecodeHandle(&channel, offset);
    *value = ClientEnd<T>(std::move(channel));
  }
};

template <typename T>
struct CodingTraits<ServerEnd<T>> {
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, ServerEnd<T>* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(maybe_handle_info);
    zx::channel channel = value->TakeChannel();
    encoder->EncodeHandle(&channel, maybe_handle_info->object_type, maybe_handle_info->rights,
                          offset);
  }

  static void Decode(Decoder* decoder, ServerEnd<T>* value, size_t offset) {
    zx::channel channel;
    decoder->DecodeHandle(&channel, offset);
    *value = ServerEnd<T>(std::move(channel));
  }
};

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_
