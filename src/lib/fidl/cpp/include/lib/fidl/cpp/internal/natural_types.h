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

template <typename T>
struct NaturalTableCodingTraits {
  static constexpr size_t inline_size_v1_no_ee = 16;
  static constexpr size_t inline_size_v2 = 16;

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
    EncodeMembers(encoder, envelope_size, value, base);
  }

  template <size_t I = 0>
  static void EncodeMembers(::fidl::Encoder* encoder, size_t envelope_size, T* value, size_t base) {
    if constexpr (I < std::tuple_size_v<decltype(T::Members)>) {
      auto member_info = std::get<I>(T::Members);
      size_t member_offset = base + (std::get<0>(member_info) - 1) * envelope_size;
      auto member_member_ptr = std::get<1>(member_info);
      auto& member_ptr = value->storage_.*(member_member_ptr);
      std::optional<fidl::HandleInformation> handle_info = std::get<2>(member_info);
      ::fidl::internal::NaturalEnvelopeEncodeOptional(encoder, &member_ptr, member_offset,
                                                      handle_info);
      // Encode the next member.
      EncodeMembers<I + 1>(encoder, envelope_size, value, base);
    }
  }

  template <size_t I = std::tuple_size_v<decltype(T::Members)> - 1>
  static size_t MaxOrdinal(T* value) {
    auto T::Storage::*member_ptr = std::get<1>(std::get<I>(T::Members));
    const auto& member = value->storage_.*member_ptr;
    if (member.has_value()) {
      return I + 1;
    }
    if constexpr (I > 0) {
      return MaxOrdinal<I - 1>(value);
    } else {
      return 0;
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

    DecodeMembers(decoder, value, base, count);
  }

  template <class DecoderImpl, size_t I = 0>
  static void DecodeMembers(DecoderImpl* decoder, T* value, size_t base, size_t count) {
    if constexpr (I < std::tuple_size_v<decltype(T::Members)>) {
      auto member_info = std::get<I>(T::Members);
      size_t member_offset = base + (std::get<0>(member_info) - 1) * sizeof(fidl_envelope_v2_t);
      auto T::Storage::*member_member_ptr = std::get<1>(member_info);
      auto& member_ptr = value->storage_.*(member_member_ptr);
      if (I < count) {
        NaturalEnvelopeDecodeOptional(decoder, &member_ptr, member_offset);
      } else {
        member_ptr.reset();
      }
      // Encode the next member.
      DecodeMembers<DecoderImpl, I + 1>(decoder, value, base, count);
    }
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
            NaturalEnvelopeEncode(encoder, &member_value, envelope_offset, T::Members[index]);
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
    if constexpr (I < std::variant_size_v<typename T::Storage>) {
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
