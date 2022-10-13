// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_TYPES_H_

#include <lib/fidl/cpp/internal/transport_err.h>
#include <lib/fidl/cpp/natural_coding_traits.h>
#include <lib/fidl/cpp/natural_encoder.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/wire_types.h>
#include <lib/fit/result.h>
#include <lib/stdcompat/type_traits.h>
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

class NaturalEncoder;

template <typename Constraint, typename Field>
void NaturalEnvelopeEncode(NaturalEncoder* encoder, Field* value, size_t offset,
                           size_t recursion_depth) {
  if (value == nullptr) {
    // Nothing to encode.
    return;
  }

  const size_t length_before = encoder->CurrentLength();
  const size_t handles_before = encoder->CurrentHandleCount();

  if (::fidl::internal::NaturalEncodingInlineSize<Field, Constraint>(encoder) <=
      FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
    fidl::internal::NaturalEncode<Constraint>(encoder, value, offset, recursion_depth);

    // Call GetPtr after Encode because the buffer may move.
    fidl_envelope_v2_t* envelope = encoder->GetPtr<fidl_envelope_v2_t>(offset);
    envelope->num_handles = static_cast<uint16_t>(encoder->CurrentHandleCount() - handles_before);
    envelope->flags = FIDL_ENVELOPE_FLAGS_INLINING_MASK;
    return;
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
void NaturalEnvelopeDecode(NaturalDecoder* decoder, Field* value, size_t offset,
                           size_t recursion_depth) {
  size_t body_size = NaturalDecodingInlineSize<Field, Constraint>(decoder);
  const size_t length_before = decoder->CurrentLength();
  const size_t handles_before = decoder->CurrentHandleCount();

  fidl_envelope_v2_t* envelope = decoder->GetPtr<fidl_envelope_v2_t>(offset);
  if (::fidl::internal::NaturalDecodingInlineSize<Field, Constraint>(decoder) <=
      FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
    if (envelope->flags != FIDL_ENVELOPE_FLAGS_INLINING_MASK) {
      decoder->SetError(kCodingErrorInvalidInlineBit);
      return;
    }

    fidl::internal::NaturalDecode<Constraint>(
        decoder, value, decoder->GetOffset(&envelope->inline_value), recursion_depth);

    if (decoder->CurrentHandleCount() != handles_before + envelope->num_handles) {
      decoder->SetError(kCodingErrorInvalidNumHandlesSpecifiedInEnvelope);
    }

    uint32_t padding;
    switch (::fidl::internal::NaturalDecodingInlineSize<Field, Constraint>(decoder)) {
      case 1:
        padding = 0xffffff00;
        break;
      case 2:
        padding = 0xffff0000;
        break;
      case 3:
        padding = 0xff000000;
        break;
      case 4:
        padding = 0x00000000;
        break;
      default:
        __builtin_unreachable();
    }
    if ((*decoder->GetPtr<uint32_t>(offset) & padding) != 0) {
      decoder->SetError(kCodingErrorInvalidPaddingBytes);
    }

    return;
  }

  if (envelope->flags != 0) {
    decoder->SetError(kCodingErrorInvalidInlineBit);
    return;
  }

  size_t body_offset;
  if (!decoder->Alloc(body_size, &body_offset)) {
    return;
  }
  fidl::internal::NaturalDecode<Constraint>(decoder, value, body_offset, recursion_depth);

  if (decoder->CurrentHandleCount() != handles_before + envelope->num_handles) {
    decoder->SetError(kCodingErrorInvalidNumHandlesSpecifiedInEnvelope);
  }
  if (decoder->CurrentLength() != length_before + envelope->num_bytes) {
    decoder->SetError(kCodingErrorInvalidNumBytesSpecifiedInEnvelope);
  }
}

template <typename Constraint, typename Field>
void NaturalEnvelopeDecodeOptional(NaturalDecoder* decoder, std::optional<Field>* value,
                                   size_t offset, size_t recursion_depth) {
  fidl_envelope_v2_t* envelope = decoder->GetPtr<fidl_envelope_v2_t>(offset);
  if (*reinterpret_cast<const void* const*>(envelope) == nullptr) {
    value->reset();
    return;
  }
  value->emplace(DefaultConstructPossiblyInvalidObject<Field>::Make());
  NaturalEnvelopeDecode<Constraint>(decoder, &value->value(), offset, recursion_depth);
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
template <typename T, typename Field_, typename Constraint_>
struct NaturalStructMember final {
  using Field = Field_;
  using Constraint = Constraint_;
  Field T::*member_ptr;
  size_t offset;
  explicit constexpr NaturalStructMember(Field T::*member_ptr, size_t offset) noexcept
      : member_ptr(member_ptr), offset(offset) {}
};

template <typename MaskType>
struct NaturalStructPadding final {
  // Offset within the struct (start of struct = 0).
  size_t offset;
  MaskType mask;

  [[nodiscard]] bool ValidatePadding(NaturalDecoder* decoder, size_t base_offset) {
    return (*decoder->GetPtr<MaskType>(base_offset + offset) & mask) == 0;
  }
};

template <typename T, size_t Size>
struct NaturalStructCodingTraits {
  static constexpr size_t inline_size_v2 = Size;
  // True iff all fields are memcpy compatible.
  static constexpr bool are_members_memcpy_compatible =
      TupleVisitor::All(T::kMembers, [](auto member_info) {
        using Field = typename std::remove_reference_t<decltype(member_info)>::Field;
        using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
        return NaturalIsMemcpyCompatible<Field, Constraint>();
      });
  // True iff fields are memcpy compatible and there is no padding.
  static constexpr bool is_memcpy_compatible =
      are_members_memcpy_compatible && std::tuple_size_v<decltype(T::kPadding)> == 0;

  static void Encode(NaturalEncoder* encoder, T* value, size_t offset, size_t recursion_depth) {
    if constexpr (is_memcpy_compatible) {
      memcpy(encoder->GetPtr<T>(offset), value, sizeof(T));
    } else {
      MemberVisitor<T>::Visit(value, [&](auto* member, auto& member_info) -> void {
        using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
        fidl::internal::NaturalEncode<Constraint>(encoder, member, offset + member_info.offset,
                                                  recursion_depth);
      });
    }
  }

  static void Decode(NaturalDecoder* decoder, T* value, size_t offset, size_t recursion_depth) {
    if constexpr (is_memcpy_compatible) {
      memcpy(value, decoder->GetPtr<T>(offset), sizeof(T));
    } else {
      MemberVisitor<T>::Visit(value, [&](auto* member, auto& member_info) {
        using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
        fidl::internal::NaturalDecode<Constraint>(decoder, member, offset + member_info.offset,
                                                  recursion_depth);
      });

      auto valid_padding_predicate = [decoder, offset](auto padding) {
        return padding.ValidatePadding(decoder, offset);
      };
      bool padding_valid = TupleVisitor::All(T::kPadding, valid_padding_predicate);
      if (!padding_valid) {
        decoder->SetError(kCodingErrorInvalidPaddingBytes);
      }
    }
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

template <typename T>
struct NaturalEmptyStructCodingTraits {
  static constexpr size_t inline_size_v2 = 1;
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(NaturalEncoder* encoder, T* value, size_t offset, size_t recursion_depth) {}

  static void Decode(NaturalDecoder* decoder, T* value, size_t offset, size_t recursion_depth) {
    if (*decoder->GetPtr<uint8_t>(offset) != 0) {
      decoder->SetError(kCodingErrorInvalidPaddingBytes);
    }
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
  static constexpr size_t inline_size_v2 = 16;
  static constexpr bool is_memcpy_compatible = false;

  struct TableMemberVisitor : public MemberVisitor<T> {
    using Base = MemberVisitor<T>;

    // Invokes |func| with |std::integral_constant<size_t, I>| for each |I| in |indexes|.
    template <typename F, size_t... I>
    static void Fold(F func, std::index_sequence<I...> indexes) {
      (func(std::integral_constant<size_t, I>{}), ...);
    }

    // Visit all of the members in order, calling |func| with the previous member ordinal and the
    // current member ordinal. The main purpose of this function is to optimize closing unknown
    // envelopes in tables. The compiler can deterministically omit unknown envelope code paths if
    // it statically knows that there is no gap between two ordinals.
    //
    // |func| should have the signature:
    //
    // void func(
    //     Member* member_ptr,
    //     const fidl::internal::NaturalTableMember<...>& member_info,
    //     std::integral_constant<size_t, PreviousOrdinal>,
    //     std::integral_constant<size_t, CurrentOrdinal>,
    // );
    template <typename U, typename Fn>
    static void VisitPrevAndCurOrdinals(U value, Fn&& func) {
      static_assert(std::is_same_v<T*, U> || std::is_same_v<const T*, U>);
      constexpr size_t N = std::tuple_size_v<decltype(T::kMembers)>;
      Fold(
          [func = std::forward<Fn>(func), value](const auto& integral) {
            constexpr size_t I = cpp20::remove_cvref_t<decltype(integral)>::value;
            auto& member_info = std::get<I>(Base::kMembers);
            auto* member_ptr = &(value->storage_.*(member_info.member_ptr));
            if constexpr (I == 0) {
              func(member_ptr, member_info, std::integral_constant<size_t, 0>{},
                   std::integral_constant<size_t, member_info.ordinal>{});
            } else {
              constexpr auto& prev_member_info = std::get<I - 1>(Base::kMembers);
              func(member_ptr, member_info,
                   std::integral_constant<size_t, prev_member_info.ordinal>{},
                   std::integral_constant<size_t, member_info.ordinal>{});
            }
          },
          std::make_index_sequence<N>{});
    }
  };

  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, T* value, size_t offset, size_t recursion_depth) {
    size_t max_ordinal = MaxOrdinalPresent(value);
    fidl_vector_t* vector = encoder->template GetPtr<fidl_vector_t>(offset);
    vector->count = max_ordinal;
    vector->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    if (max_ordinal == 0)
      return;
    if (recursion_depth + 2 > kRecursionDepthMax) {
      encoder->SetError(kCodingErrorRecursionDepthExceeded);
      return;
    }

    size_t base = encoder->Alloc(max_ordinal * sizeof(fidl_envelope_v2_t));
    MemberVisitor<T>::Visit(value, [&](auto* member, auto& member_info) {
      size_t offset = base + (member_info.ordinal - 1) * sizeof(fidl_envelope_v2_t);
      using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
      NaturalEnvelopeEncodeOptional<Constraint>(encoder, member, offset, recursion_depth + 2);
    });
  }

  // Returns the largest ordinal of a present table member.
  template <size_t I = std::tuple_size_v<decltype(T::kMembers)> - 1>
  static size_t MaxOrdinalPresent(T* value) {
    if constexpr (I == -1) {
      return 0;
    } else {
      auto T::Storage_::*member_ptr = std::get<I>(T::kMembers).member_ptr;
      const auto& member = value->storage_.*member_ptr;
      if (member.has_value()) {
        return std::get<I>(T::kMembers).ordinal;
      }
      return MaxOrdinalPresent<I - 1>(value);
    }
  }

  // Returns the largest known ordinal in the FIDL schema.
  constexpr static size_t MaxOrdinalInSchema() {
    constexpr size_t kMemberCount = std::tuple_size_v<decltype(T::kMembers)>;
    if constexpr (kMemberCount == 0) {
      return 0;
    } else {
      return std::get<kMemberCount - 1>(T::kMembers).ordinal;
    }
  }

  static void Decode(NaturalDecoder* decoder, T* value, size_t offset, size_t recursion_depth) {
    fidl_vector_t* encoded = decoder->template GetPtr<fidl_vector_t>(offset);

    switch (reinterpret_cast<uintptr_t>(encoded->data)) {
      case FIDL_ALLOC_PRESENT:
        break;
      case FIDL_ALLOC_ABSENT: {
        decoder->SetError(kCodingErrorNullDataReceivedForTable);
        return;
      }
      default: {
        decoder->SetError(kCodingErrorInvalidPresenceIndicator);
        return;
      }
    }
    if (recursion_depth + 2 > kRecursionDepthMax) {
      decoder->SetError(kCodingErrorRecursionDepthExceeded);
      return;
    }

    size_t count = encoded->count;
    size_t base;
    if (!decoder->Alloc(sizeof(fidl_envelope_v2_t) * count, &base)) {
      return;
    }
    auto envelope_offset = [base](size_t ordinal) {
      return base + (ordinal - 1) * sizeof(fidl_envelope_v2_t);
    };

    // While visiting, if there's a gap in the ordinals, close the envelopes in the gaps.
    // The gaps between 1 to first member ordinal, and the gaps between last member ordinal
    // to count, must also be closed.
    TableMemberVisitor::VisitPrevAndCurOrdinals(value, [=](auto* member, const auto& member_info,
                                                           const auto& prev_ordinal_integral,
                                                           const auto& ordinal_integral) {
      constexpr size_t kPrevOrdinal = cpp20::remove_cvref_t<decltype(prev_ordinal_integral)>::value;
      constexpr size_t kMemberOrdinal = cpp20::remove_cvref_t<decltype(ordinal_integral)>::value;
      if (member_info.ordinal > count) {
        member->reset();
        return;
      }
      // This block will disappear if there are no reserved ordinals in-between.
      if constexpr (kPrevOrdinal + 1 < kMemberOrdinal) {
        for (size_t i = kPrevOrdinal + 1; i < kMemberOrdinal; i++) {
          decoder->DecodeUnknownEnvelopeOptional(envelope_offset(i));
        }
      }
      size_t member_offset = envelope_offset(member_info.ordinal);
      using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
      NaturalEnvelopeDecodeOptional<Constraint>(decoder, member, member_offset, recursion_depth);
    });
    for (size_t i = MaxOrdinalInSchema() + 1; i < count + 1; i++) {
      decoder->DecodeUnknownEnvelopeOptional(envelope_offset(i));
    }
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
  static constexpr size_t inline_size_v2 = 16;
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(NaturalEncoder* encoder, T* value, size_t offset, size_t recursion_depth) {
    const size_t index = value->storage_->index();
    if (unlikely(index == 0)) {
      // While it is not possible to construct a flexible union with unknown
      // data in the natural C++ types, this may happen if e.g. someone tried
      // to re-encode a flexible union that's received with unknown data.
      encoder->SetError(kCodingErrorUnknownUnionTag);
      return;
    }
    if (unlikely(recursion_depth + 1 > kRecursionDepthMax)) {
      encoder->SetError(kCodingErrorRecursionDepthExceeded);
      return;
    }
    const size_t envelope_offset = offset + offsetof(fidl_xunion_t, envelope);
    EncodeMember(encoder, value, envelope_offset, index, recursion_depth + 1);
    // Call GetPtr after Encode because the buffer may move.
    fidl_xunion_v2_t* xunion = encoder->GetPtr<fidl_xunion_v2_t>(offset);
    xunion->tag = static_cast<fidl_union_tag_t>(T::IndexToTag(index).value());
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

  static void Decode(NaturalDecoder* decoder, T* value, size_t offset, size_t recursion_depth) {
    // Note: fidl_xunion_t and fidl_xunion_v2_t have xunion->tag in the same layout position
    // and the same value of offsetof(fidl_xunion_t, envelope).
    static_assert(sizeof(fidl_xunion_t::tag) == sizeof(fidl_xunion_v2_t::tag));
    static_assert(offsetof(fidl_xunion_t, tag) == offsetof(fidl_xunion_v2_t, tag));
    static_assert(offsetof(fidl_xunion_t, envelope) == offsetof(fidl_xunion_v2_t, envelope));

    fidl_xunion_v2_t* xunion = decoder->GetPtr<fidl_xunion_v2_t>(offset);
    const size_t index = T::TagToIndex(decoder, static_cast<typename T::Tag>(xunion->tag));
    if (unlikely(decoder->status() != ZX_OK)) {
      return;
    }
    if (unlikely(recursion_depth + 1 > kRecursionDepthMax)) {
      decoder->SetError(kCodingErrorRecursionDepthExceeded);
      return;
    }
    const size_t envelope_offset = offset + offsetof(fidl_xunion_v2_t, envelope);
    if (unlikely(index == 0)) {
      // Flexible unknown envelope.
      decoder->DecodeUnknownEnvelopeRequired(envelope_offset);
      if (unlikely(xunion->tag == 0)) {
        decoder->SetError(kCodingErrorZeroTagButNonZeroEnvelope);
      }
      return;
    }
    DecodeMember(decoder, value, envelope_offset, index, recursion_depth + 1);
  }

  template <size_t I = 1>
  static void DecodeMember(NaturalDecoder* decoder, T* value, size_t envelope_offset,
                           const size_t index, size_t recursion_depth) {
    static_assert(I > 0);
    if constexpr (I < std::variant_size_v<typename T::Storage_>) {
      if (I == index) {
        using FieldType = cpp20::remove_cvref_t<decltype(std::get<I>(*value->storage_))>;
        value->storage_->template emplace<I>(
            DefaultConstructPossiblyInvalidObject<FieldType>::Make());
        auto& member_info = std::get<I>(T::kMembers);
        using Constraint = typename std::remove_reference_t<decltype(member_info)>::Constraint;
        NaturalEnvelopeDecode<Constraint>(decoder, &std::get<I>(*value->storage_), envelope_offset,
                                          recursion_depth);
        // Success.
        return;
      }
      // Tail recurse into attempting to decode as the next member.
      return DecodeMember<I + 1>(decoder, value, envelope_offset, index, recursion_depth);
    }
    // Unknown member case is handled in |Decode|.
    __builtin_unreachable();
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
