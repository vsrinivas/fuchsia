// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_

#include <lib/fidl/cpp/natural_coding_traits.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/wire_types.h>
#include <lib/fitx/result.h>
#include <zircon/fidl.h>

#include <algorithm>
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
using Error = Status;

namespace internal {

template <typename Constraint, typename Field>
void NaturalEnvelopeEncode(NaturalEncoder* encoder, Field* value, size_t offset,
                           size_t recursion_depth) {
  if (value == nullptr) {
    // Nothing to encode.
    return;
  }

  const size_t length_before = encoder->CurrentLength();
  const size_t handles_before = encoder->CurrentHandleCount();
  switch (encoder->wire_format()) {
    case ::fidl::internal::WireFormatVersion::kV1: {
      fidl::internal::NaturalEncode<Constraint>(
          encoder, value,
          encoder->Alloc(::fidl::internal::NaturalEncodingInlineSize<Field, Constraint>(encoder)),
          recursion_depth);

      // Call GetPtr after Encode because the buffer may move.
      fidl_envelope_t* envelope = encoder->GetPtr<fidl_envelope_t>(offset);
      envelope->num_bytes = static_cast<uint32_t>(encoder->CurrentLength() - length_before);
      envelope->num_handles = static_cast<uint32_t>(encoder->CurrentHandleCount() - handles_before);
      envelope->presence = FIDL_ALLOC_PRESENT;
      break;
    }
    case ::fidl::internal::WireFormatVersion::kV2: {
      if (::fidl::internal::NaturalEncodingInlineSize<Field, Constraint>(encoder) <=
          FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
        fidl::internal::NaturalEncode<Constraint>(encoder, value, offset, recursion_depth);

        // Call GetPtr after Encode because the buffer may move.
        fidl_envelope_v2_t* envelope = encoder->GetPtr<fidl_envelope_v2_t>(offset);
        envelope->num_handles =
            static_cast<uint16_t>(encoder->CurrentHandleCount() - handles_before);
        envelope->flags = FIDL_ENVELOPE_FLAGS_INLINING_MASK;
        break;
      }

      fidl::internal::NaturalEncode<Constraint>(
          encoder, value,
          encoder->Alloc(::fidl::internal::NaturalEncodingInlineSize<Field, Constraint>(encoder)),
          recursion_depth);

      // Call GetPtr after Encode because the buffer may move.
      fidl_envelope_v2_t* envelope = encoder->GetPtr<fidl_envelope_v2_t>(offset);
      envelope->num_bytes = static_cast<uint32_t>(encoder->CurrentLength() - length_before);
      envelope->num_handles = static_cast<uint16_t>(encoder->CurrentHandleCount() - handles_before);
      envelope->flags = 0;
      break;
    }
  }
}

template <typename Constraint, typename Field>
void NaturalEnvelopeEncodeOptional(NaturalEncoder* encoder, std::optional<Field>* value,
                                   size_t offset, size_t recursion_depth) {
  if (!value->has_value()) {
    // Nothing to encode.
    return;
  }
  NaturalEnvelopeEncode<Constraint>(encoder, &value->value(), offset, recursion_depth);
}

template <typename Constraint, typename Field>
void NaturalEnvelopeDecodeOptional(NaturalDecoder* decoder, std::optional<Field>* value,
                                   size_t offset) {
  fidl_envelope_v2_t* envelope = decoder->GetPtr<fidl_envelope_v2_t>(offset);
  if (*reinterpret_cast<const void* const*>(envelope) != nullptr) {
    value->emplace();
    fidl::internal::NaturalDecode<Constraint>(decoder, &value->value(),
                                              decoder->EnvelopeValueOffset(envelope));
  } else {
    value->reset();
  }
}

template <typename Constraint, typename Field>
bool NaturalEnvelopeDecode(NaturalDecoder* decoder, Field* value, size_t offset) {
  fidl_envelope_v2_t* envelope = decoder->GetPtr<fidl_envelope_v2_t>(offset);
  if (*reinterpret_cast<const void* const*>(envelope) != nullptr) {
    fidl::internal::NaturalDecode<Constraint>(decoder, value,
                                              decoder->EnvelopeValueOffset(envelope));
    return true;
  } else {
    return false;
  }
}

// MemberVisitor provides helpers to invoke visitor functions over natural struct and natural table
// members. This works because structs and tables have similar shapes in the natural bindings.
// There is an instance data member called `storage_` which is a struct containing the member data
// and a constexpr std::tuple member called `kMembers`, and each member of that has a `member_ptr`
// which is a member pointer into the `storage_` struct.
template <typename T>
struct MemberVisitor {
  static constexpr auto kMembers = T::kMembers;
  static constexpr size_t kNumMembers = std::tuple_size_v<decltype(kMembers)>;

  // Visit each of the members in order while the visitor function returns a truthy value.
  template <typename U, typename Fn, size_t I = 0>
  static void VisitWhile(U value, Fn func) {
    static_assert(std::is_same_v<T*, U> || std::is_same_v<const T*, U>);
    if constexpr (I < kNumMembers) {
      auto& member_info = std::get<I>(kMembers);
      auto* member_ptr = &(value->storage_.*(member_info.member_ptr));

      if (func(member_ptr, member_info)) {
        VisitWhile<U, Fn, I + 1>(value, func);
      }
    }
  }

  // Visit all of the members in order.
  template <typename U, typename Fn>
  static void Visit(U value, Fn func) {
    static_assert(std::is_same_v<T*, U> || std::is_same_v<const T*, U>);
    VisitWhile(value, [func = std::move(func)](auto member_ptr, auto member_info) {
      func(member_ptr, member_info);
      return true;
    });
  }

  // Visit each of the members of two structs or tables in order while the visitor function returns
  // a truthy value.
  template <typename U, typename Fn, size_t I = 0>
  static void Visit2While(U value1, U value2, Fn func) {
    static_assert(std::is_same_v<T*, U> || std::is_same_v<const T*, U>);
    if constexpr (I < std::tuple_size_v<decltype(T::kMembers)>) {
      auto& member_info = std::get<I>(T::kMembers);
      auto* member_ptr1 = &(value1->storage_.*(member_info.member_ptr));
      auto* member_ptr2 = &(value2->storage_.*(member_info.member_ptr));

      if (func(member_ptr1, member_ptr2, member_info)) {
        Visit2While<U, Fn, I + 1>(value1, value2, func);
      }
    }
  }

  // Visit all of the members of two structs or tables in order.
  template <typename U, typename Fn>
  static void Visit2(U value1, U value2, Fn func) {
    static_assert(std::is_same_v<T*, U> || std::is_same_v<const T*, U>);
    Visit2While(value1, value2,
                [func = std::move(func)](auto member1, auto member2, auto member_info) {
                  func(member1, member2, member_info);
                  return true;
                });
  }
};

// This holds metadata about a struct member: a member pointer to the member's value in
// the struct's Storage_ type, offsets, and optionally handle information.
template <typename T, typename Field, typename Constraint_>
struct NaturalStructMember final {
  using Constraint = Constraint_;
  Field T::*member_ptr;
  size_t offset_v1;
  size_t offset_v2;
  explicit constexpr NaturalStructMember(Field T::*member_ptr, size_t offset_v1,
                                         size_t offset_v2) noexcept
      : member_ptr(member_ptr), offset_v1(offset_v1), offset_v2(offset_v2) {}
};

template <typename T, size_t V1, size_t V2>
struct NaturalStructCodingTraits {
  static constexpr size_t inline_size_v1_no_ee = V1;
  static constexpr size_t inline_size_v2 = V2;

  static void Encode(NaturalEncoder* encoder, T* value, size_t offset, size_t recursion_depth) {
    const auto wire_format = encoder->wire_format();
    MemberVisitor<T>::Visit(value, [&](auto* member, auto& member_info) -> void {
      size_t field_offset = wire_format == ::fidl::internal::WireFormatVersion::kV1
                                ? member_info.offset_v1
                                : member_info.offset_v2;
      using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
      fidl::internal::NaturalEncode<Constraint>(encoder, member, offset + field_offset,
                                                recursion_depth);
    });
  }

  static void Decode(NaturalDecoder* decoder, T* value, size_t offset) {
    MemberVisitor<T>::Visit(value, [&](auto* member, auto& member_info) {
      using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
      fidl::internal::NaturalDecode<Constraint>(decoder, member, offset + member_info.offset_v2);
    });
  }

  static bool Equal(const T* struct1, const T* struct2) {
    bool equal = true;
    MemberVisitor<T>::Visit2(
        struct1, struct2, [&](const auto* member1, const auto* member2, auto& member_info) -> bool {
          if (*member1 != *member2) {
            equal = false;
            return false;
          }
          return true;
        });
    return equal;
  }
};

// This holds metadata about a table member: its ordinal, a member pointer to the member's value in
// the table's Storage_ type, and optionally handle information.
template <typename T, typename Field, typename Constraint_>
struct NaturalTableMember final {
  using Constraint = Constraint_;
  size_t ordinal;
  std::optional<Field> T::*member_ptr;
  constexpr NaturalTableMember(size_t ordinal, std::optional<Field> T::*member_ptr) noexcept
      : ordinal(ordinal), member_ptr(member_ptr) {}
};

template <typename T>
struct NaturalTableCodingTraits {
  static constexpr size_t inline_size_v1_no_ee = 16;
  static constexpr size_t inline_size_v2 = 16;

  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, T* value, size_t offset, size_t recursion_depth) {
    size_t max_ordinal = MaxOrdinal(value);
    fidl_vector_t* vector = encoder->template GetPtr<fidl_vector_t>(offset);
    vector->count = max_ordinal;
    vector->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    if (max_ordinal == 0)
      return;
    if (recursion_depth + 2 > kRecursionDepthMax) {
      encoder->SetError("recursion depth exceeded");
      return;
    }
    size_t envelope_size = (encoder->wire_format() == ::fidl::internal::WireFormatVersion::kV1)
                               ? sizeof(fidl_envelope_t)
                               : sizeof(fidl_envelope_v2_t);
    size_t base = encoder->Alloc(max_ordinal * envelope_size);
    MemberVisitor<T>::Visit(value, [&](auto* member, auto& member_info) {
      size_t offset = base + (member_info.ordinal - 1) * envelope_size;
      using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
      NaturalEnvelopeEncodeOptional<Constraint>(encoder, member, offset, recursion_depth + 2);
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
        return std::get<I>(T::kMembers).ordinal;
      }
      return MaxOrdinal<I - 1>(value);
    }
  }

  static void Decode(NaturalDecoder* decoder, T* value, size_t offset) {
    fidl_vector_t* encoded = decoder->template GetPtr<fidl_vector_t>(offset);

    if (!encoded->data) {
      *value = T{};
      return;
    }

    size_t base = decoder->GetOffset(encoded->data);
    size_t count = encoded->count;
    constexpr size_t envelope_size = sizeof(fidl_envelope_v2_t);

    MemberVisitor<T>::Visit(value, [&](auto* member, auto& member_info) {
      size_t member_offset = base + (member_info.ordinal - 1) * envelope_size;
      if (member_info.ordinal <= count) {
        using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
        NaturalEnvelopeDecodeOptional<Constraint>(decoder, member, member_offset);
      } else {
        member->reset();
      }
    });
  }

  static bool Equal(const T* table1, const T* table2) {
    bool equal = true;
    MemberVisitor<T>::Visit2(table1, table2,
                             [&](const auto* member1, const auto* member2, auto& member_info) {
                               if (*member1 != *member2) {
                                 equal = false;
                               }
                             });
    return equal;
  }
};

// This holds metadata about a union member: a member pointer to the member's value in
// the unions's Storage_ type, offsets, and optionally handle information.
template <typename Constraint_>
struct NaturalUnionMember final {
  using Constraint = Constraint_;
};

template <typename T>
struct NaturalUnionCodingTraits {
  static constexpr size_t inline_size_v1_no_ee = 24;
  static constexpr size_t inline_size_v2 = 16;

  static void Encode(NaturalEncoder* encoder, T* value, size_t offset, size_t recursion_depth) {
    const size_t index = value->storage_->index();
    ZX_ASSERT(index > 0);
    if (recursion_depth + 1 > kRecursionDepthMax) {
      encoder->SetError("recursion depth exceeded");
      return;
    }
    const size_t envelope_offset = offset + offsetof(fidl_xunion_t, envelope);
    EncodeMember(encoder, value, envelope_offset, index, recursion_depth + 1);
    // Call GetPtr after Encode because the buffer may move.
    fidl_xunion_v2_t* xunion = encoder->GetPtr<fidl_xunion_v2_t>(offset);
    xunion->tag = static_cast<fidl_union_tag_t>(T::IndexToTag(index));
  }

  template <size_t I = 1>
  static void EncodeMember(NaturalEncoder* encoder, T* value, size_t envelope_offset,
                           const size_t index, size_t recursion_depth) {
    static_assert(I > 0);
    if constexpr (I < std::variant_size_v<typename T::Storage_>) {
      if (I == index) {
        auto& member_info = std::get<I>(T::kMembers);
        using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
        NaturalEnvelopeEncode<Constraint>(encoder, &std::get<I>(*value->storage_), envelope_offset,
                                          recursion_depth);
        return;
      }
      return EncodeMember<I + 1>(encoder, value, envelope_offset, index, recursion_depth);
    }
  }

  static void Decode(NaturalDecoder* decoder, T* value, size_t offset) {
    fidl_xunion_v2_t* xunion = decoder->GetPtr<fidl_xunion_v2_t>(offset);
    const size_t index = T::TagToIndex(static_cast<typename T::Tag>(xunion->tag));
    ZX_ASSERT(index > 0);
    const size_t envelope_offset = offset + offsetof(fidl_xunion_t, envelope);
    DecodeMember(decoder, value, envelope_offset, index);
  }

  template <size_t I = 1>
  static void DecodeMember(NaturalDecoder* decoder, T* value, size_t envelope_offset,
                           const size_t index) {
    static_assert(I > 0);
    if constexpr (I < std::variant_size_v<typename T::Storage_>) {
      if (I == index) {
        value->storage_->template emplace<I>();
        auto& member_info = std::get<I>(T::kMembers);
        using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
        NaturalEnvelopeDecode<Constraint>(decoder, &std::get<I>(*value->storage_), envelope_offset);
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

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_
