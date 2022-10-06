// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_CODING_TRAITS_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_CODING_TRAITS_H_

#include <lib/fidl/cpp/wire/coding_errors.h>
#include <lib/fidl/cpp/wire/object_view.h>
#include <lib/fidl/cpp/wire/optional.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/fidl/cpp/wire/wire_decoder.h>
#include <lib/fidl/cpp/wire/wire_encoder.h>
#include <lib/fidl/cpp/wire_format_metadata.h>
#include <lib/utf-utils/utf-utils.h>
#include <zircon/types.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#endif  // __Fuchsia__

// Forward declarations

namespace fidl {

class EncodedMessage;

}  // namespace fidl

namespace fidl::internal {

struct WireCodingConstraintEmpty {};

template <zx_obj_type_t ObjType, zx_rights_t Rights, bool IsOptional>
struct WireCodingConstraintHandle {
  static constexpr zx_obj_type_t obj_type = ObjType;
  static constexpr zx_rights_t rights = Rights;
  static constexpr bool is_optional = IsOptional;
};

template <bool IsOptional, size_t Limit = std::numeric_limits<size_t>::max()>
struct WireCodingConstraintString {
  static constexpr size_t limit = Limit;
  static constexpr bool is_optional = IsOptional;
};

template <typename Inner_, bool IsOptional, size_t Limit = std::numeric_limits<size_t>::max()>
struct WireCodingConstraintVector {
  using Inner = Inner_;
  static constexpr size_t limit = Limit;
  static constexpr bool is_optional = IsOptional;
};

template <bool IsOptional>
struct WireCodingConstraintUnion {
  static constexpr bool is_optional = IsOptional;
};

template <typename T, typename Constraint, bool IsRecursive, class Enable = void>
struct WireCodingTraits;

template <typename T, typename Constraint, bool IsRecursive>
constexpr bool WireIsMemcpyCompatible() {
  return WireCodingTraits<T, Constraint, IsRecursive>::is_memcpy_compatible;
}

template <typename MaskType>
void WireZeroPadding(WireEncoder* encoder, WirePosition position) {
  *position.As<MaskType>() = 0;
}

template <typename MaskType>
void WireCheckPadding(WireDecoder* decoder, WirePosition position, MaskType mask) {
  if (unlikely(*position.As<MaskType>() & mask)) {
    decoder->SetError(kCodingErrorInvalidPaddingBytes);
  }
}

template <typename T, bool IsRecursive>
struct WireCodingTraits<T, WireCodingConstraintEmpty, IsRecursive,
                        typename std::enable_if<IsPrimitive<T>::value>::type> {
  static constexpr size_t inline_size = sizeof(T);
  static constexpr bool is_memcpy_compatible = true;

  static void Encode(WireEncoder* encoder, T* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    *position.As<T>() = *value;
  }
  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {}
};

template <bool IsRecursive>
struct WireCodingTraits<bool, WireCodingConstraintEmpty, IsRecursive> {
  static constexpr size_t inline_size = sizeof(bool);
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(WireEncoder* encoder, const bool* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    *position.As<bool>() = *value;
  }
  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    uint8_t uintval = *position.As<uint8_t>();
    if (unlikely(!(uintval == 0 || uintval == 1))) {
      decoder->SetError(kCodingErrorInvalidBoolean);
      return;
    }
  }
};

template <bool IsRecursive>
struct VectorCodingTraitHelper {
  enum class PreworkResult : bool {
    kSuccess,
    kEarlyExit,
  };

  template <typename T, typename Constraint>
  static void Encode(WireEncoder* encoder, T* data, size_t count, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    if (PreworkResult::kEarlyExit ==
        EncodePrework(encoder, data, count, position, Constraint::is_optional, Constraint::limit)) {
      return;
    }
    RecursionDepth<IsRecursive> inner_depth = recursion_depth.Add(encoder, 1);
    if (!inner_depth.IsValid()) {
      return;
    }
    using InnerConstraint = typename Constraint::Inner;
    if constexpr (WireIsMemcpyCompatible<T, InnerConstraint, IsRecursive>()) {
      constexpr size_t stride = WireCodingTraits<T, InnerConstraint, IsRecursive>::inline_size;
      static_assert(stride <= std::numeric_limits<uint32_t>::max(),
                    "assume 32-bit stride to reduce checks");
      encoder->EncodeMemcpyableVector(data, count, stride);
    } else {
      EncodeEachElement<T, InnerConstraint>(encoder, data, count, position, inner_depth);
    }
  }

  template <typename T, typename InnerConstraint>
  static void EncodeEachElement(WireEncoder* encoder, T* data, size_t count, WirePosition position,
                                RecursionDepth<IsRecursive> recursion_depth) {
    constexpr size_t stride = WireCodingTraits<T, InnerConstraint, IsRecursive>::inline_size;
    static_assert(stride <= std::numeric_limits<uint32_t>::max(),
                  "assume 32-bit stride to reduce checks");

    WirePosition base;
    if (unlikely(!encoder->Alloc(count * stride, &base))) {
      return;
    }
    for (uint32_t i = 0; i < count; i++) {
      WireCodingTraits<T, InnerConstraint, IsRecursive>::Encode(encoder, &data[i],
                                                                base + i * stride, recursion_depth);
    }
  }

  static PreworkResult EncodePrework(WireEncoder* encoder, const void* data, size_t count,
                                     WirePosition position, bool is_optional, size_t limit) {
    if (unlikely(data == nullptr)) {
      if (is_optional) {
        *position.As<fidl_vector_t>() = {
            .count = 0,
            .data = reinterpret_cast<void*>(FIDL_ALLOC_ABSENT),
        };
      } else {
        // Note: it would normally be an error to encode null data in a non-nullable vector,
        // but for ergonomics reasons this is treated as a 0 length vector.
        // It must have a present data portion as required by the FIDL wire format spec.
        *position.As<fidl_vector_t>() = {
            .count = 0,
            .data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT),
        };
      }
      return PreworkResult::kEarlyExit;
    }
    if (unlikely(count > limit)) {
      encoder->SetError(kCodingErrorVectorLimitExceeded);
      return PreworkResult::kEarlyExit;
    }

    *position.As<fidl_vector_t>() = {
        .count = count,
        .data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT),
    };

    if (unlikely(count == 0)) {
      return PreworkResult::kEarlyExit;
    }
    return PreworkResult::kSuccess;
  }

  template <typename T, typename Constraint>
  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    using InnerConstraint = typename Constraint::Inner;
    if (PreworkResult::kEarlyExit ==
        DecodePrework(decoder, position, Constraint::is_optional, Constraint::limit)) {
      return;
    }
    RecursionDepth inner_depth = recursion_depth.Add(decoder, 1);
    if (!inner_depth.IsValid()) {
      return;
    }
    constexpr size_t stride = WireCodingTraits<T, InnerConstraint, IsRecursive>::inline_size;
    static_assert(stride <= std::numeric_limits<uint32_t>::max(),
                  "assume 32-bit stride to reduce checks");
    fidl_vector_t* encoded = position.As<fidl_vector_t>();
    WirePosition base;
    if (unlikely(!decoder->Alloc(encoded->count * stride, &base))) {
      return;
    }
    encoded->data = base.As<void>();
    if constexpr (!WireIsMemcpyCompatible<T, InnerConstraint, IsRecursive>()) {
      for (uint32_t i = 0; i < encoded->count; i++) {
        WireCodingTraits<T, InnerConstraint, IsRecursive>::Decode(decoder, base + i * stride,
                                                                  inner_depth);
      }
    }
  }

  static PreworkResult DecodePrework(WireDecoder* decoder, WirePosition position, bool is_optional,
                                     size_t limit) {
    fidl_vector_t* encoded = position.As<fidl_vector_t>();
    size_t count = encoded->count;
    if (unlikely(count > limit)) {
      decoder->SetError(kCodingErrorVectorLimitExceeded);
      return PreworkResult::kEarlyExit;
    }
    if (unlikely(count > std::numeric_limits<uint32_t>::max())) {
      decoder->SetError(kCodingErrorAllocationSizeExceeds32Bits);
      return PreworkResult::kEarlyExit;
    }
    switch (reinterpret_cast<uintptr_t>(encoded->data)) {
      case FIDL_ALLOC_PRESENT:
        break;
      case FIDL_ALLOC_ABSENT: {
        if (!is_optional) {
          decoder->SetError(kCodingErrorNullDataReceivedForNonNullableVector);
          return PreworkResult::kEarlyExit;
        }
        if (unlikely(count != 0)) {
          decoder->SetError(kCodingErrorNullVectorMustHaveSizeZero);
        }
        return PreworkResult::kEarlyExit;
      }
      default: {
        decoder->SetError(kCodingErrorInvalidPresenceIndicator);
        return PreworkResult::kEarlyExit;
      }
    }
    return PreworkResult::kSuccess;
  }
};

template <typename T, typename Constraint, bool IsRecursive>
struct WireCodingTraits<fidl::VectorView<T>, Constraint, IsRecursive> {
  static constexpr size_t inline_size = sizeof(fidl_vector_t);
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(WireEncoder* encoder, fidl::VectorView<T>* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    VectorCodingTraitHelper<IsRecursive>::template Encode<T, Constraint>(
        encoder, value->data(), value->count(), position, recursion_depth);
  }
  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    VectorCodingTraitHelper<IsRecursive>::template Decode<T, Constraint>(decoder, position,
                                                                         recursion_depth);
  }
};

template <typename Constraint, bool IsRecursive>
struct WireCodingTraits<fidl::StringView, Constraint, IsRecursive> final {
  static constexpr size_t inline_size = sizeof(fidl_string_t);
  static constexpr bool is_memcpy_compatible = false;
  using StringVectorConstraint =
      WireCodingConstraintVector<WireCodingConstraintEmpty, Constraint::is_optional,
                                 Constraint::limit>;

  static void Encode(WireEncoder* encoder, const fidl::StringView* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    VectorCodingTraitHelper<IsRecursive>::template Encode<const char, StringVectorConstraint>(
        encoder, value->data(), value->size(), position, recursion_depth);
    if (unlikely(value->data() == nullptr || encoder->HasError())) {
      return;
    }
    if (unlikely(!utfutils_is_valid_utf8(value->data(), value->size()))) {
      encoder->SetError(kCodingErrorStringNotValidUtf8);
      return;
    }
  }
  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    VectorCodingTraitHelper<IsRecursive>::template Decode<const char, StringVectorConstraint>(
        decoder, position, recursion_depth);
    fidl_string_t* string = position.As<fidl_string_t>();
    if (unlikely(string->data == nullptr || decoder->HasError())) {
      return;
    }
    if (unlikely(!utfutils_is_valid_utf8(string->data, string->size))) {
      decoder->SetError(kCodingErrorStringNotValidUtf8);
      return;
    }
  }
};

template <typename T, size_t N, typename Constraint, bool IsRecursive>
struct WireCodingTraits<fidl::Array<T, N>, Constraint, IsRecursive> {
  static constexpr size_t inline_size =
      WireCodingTraits<T, Constraint, IsRecursive>::inline_size * N;
  static constexpr bool is_memcpy_compatible =
      WireCodingTraits<T, Constraint, IsRecursive>::is_memcpy_compatible;

  static void Encode(WireEncoder* encoder, fidl::Array<T, N>* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    constexpr size_t stride = WireCodingTraits<T, Constraint, IsRecursive>::inline_size;
    if constexpr (is_memcpy_compatible) {
      memcpy(position.As<void>(), &value[0], N * stride);
    } else {
      for (size_t i = 0; i < N; ++i) {
        WireCodingTraits<T, Constraint, IsRecursive>::Encode(
            encoder, &value->at(i), position + i * stride, recursion_depth);
      }
    }
  }
  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    constexpr size_t stride = WireCodingTraits<T, Constraint, IsRecursive>::inline_size;
    if constexpr (!is_memcpy_compatible) {
      for (size_t i = 0; i < N; ++i) {
        WireCodingTraits<T, Constraint, IsRecursive>::Decode(decoder, position + i * stride,
                                                             recursion_depth);
      }
    }
  }
};

#ifdef __Fuchsia__
template <typename T, typename Constraint, bool IsRecursive>
struct WireCodingTraits<T, Constraint, IsRecursive,
                        typename std::enable_if<std::is_base_of<zx::object_base, T>::value>::type> {
  static constexpr size_t inline_size = sizeof(zx_handle_t);
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(WireEncoder* encoder, zx::object_base* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    encoder->EncodeHandle(value->release(),
                          {
                              .obj_type = Constraint::obj_type,
                              .rights = Constraint::rights,
                          },
                          position, Constraint::is_optional);
  }
  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    decoder->DecodeHandle(position,
                          {
                              .obj_type = Constraint::obj_type,
                              .rights = Constraint::rights,
                          },
                          Constraint::is_optional);
  }
};
#endif  // __Fuchsia__

template <typename T, typename Constraint, bool IsRecursive>
struct WireCodingTraits<fidl::ObjectView<T>, Constraint, IsRecursive> {
  static constexpr size_t inline_size = sizeof(uintptr_t);
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(WireEncoder* encoder, fidl::ObjectView<T>* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    if (!value->get()) {
      *position.As<uintptr_t>() = FIDL_ALLOC_ABSENT;
      return;
    }

    RecursionDepth<IsRecursive> inner_depth = recursion_depth.Add(encoder, 1);
    if (!inner_depth.IsValid()) {
      return;
    }

    *position.As<uintptr_t>() = FIDL_ALLOC_PRESENT;

    constexpr size_t alloc_size = WireCodingTraits<T, Constraint, IsRecursive>::inline_size;
    WirePosition body;
    if (unlikely(!encoder->Alloc(alloc_size, &body))) {
      return;
    }
    WireCodingTraits<T, Constraint, IsRecursive>::Encode(encoder, value->get(), body, inner_depth);
  }
  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    switch (*position.As<uintptr_t>()) {
      case FIDL_ALLOC_PRESENT:
        break;
      case FIDL_ALLOC_ABSENT: {
        return;
      }
      default: {
        decoder->SetError(kCodingErrorInvalidPresenceIndicator);
        return;
      }
    }
    RecursionDepth<IsRecursive> inner_depth = recursion_depth.Add(decoder, 1);
    if (!inner_depth.IsValid()) {
      return;
    }
    constexpr size_t alloc_size = WireCodingTraits<T, Constraint, IsRecursive>::inline_size;
    WirePosition body;
    if (unlikely(!decoder->Alloc(alloc_size, &body))) {
      return;
    }
    *position.As<void*>() = body.As<void>();
    WireCodingTraits<T, Constraint, IsRecursive>::Decode(decoder, body, inner_depth);
  }
};

template <typename T, typename Constraint, bool IsRecursive>
struct WireCodingTraits<fidl::WireOptional<T>, Constraint, IsRecursive> {
 private:
  using MemberTrait = WireCodingTraits<T, Constraint, IsRecursive>;

 public:
  static constexpr size_t inline_size = MemberTrait::inline_size;
  static constexpr bool is_memcpy_compatible = MemberTrait::is_memcpy_compatible;

  static void Encode(internal::WireEncoder* encoder, fidl::WireOptional<T>* value,
                     fidl::internal::WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    return MemberTrait::Encode(encoder, value, position, recursion_depth);
  }

  static void Decode(internal::WireDecoder* decoder, fidl::internal::WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    return MemberTrait::Decode(decoder, position, recursion_depth);
  }
};

#ifdef __Fuchsia__
template <typename T, typename Constraint, bool IsRecursive>
struct WireCodingTraits<ClientEnd<T>, Constraint, IsRecursive> {
  static constexpr bool is_memcpy_compatible = false;
  static constexpr size_t inline_size = sizeof(zx_handle_t);

  static void Encode(WireEncoder* encoder, ClientEnd<T>* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    encoder->EncodeHandle(value->TakeChannel().release(),
                          {
                              .obj_type = Constraint::obj_type,
                              .rights = Constraint::rights,
                          },
                          position, Constraint::is_optional);
  }

  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    decoder->DecodeHandle(position,
                          {
                              .obj_type = Constraint::obj_type,
                              .rights = Constraint::rights,
                          },
                          Constraint::is_optional);
  }
};

template <typename T, typename Constraint, bool IsRecursive>
struct WireCodingTraits<ServerEnd<T>, Constraint, IsRecursive> {
  static constexpr bool is_memcpy_compatible = false;
  static constexpr size_t inline_size = sizeof(zx_handle_t);

  static void Encode(WireEncoder* encoder, ServerEnd<T>* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    encoder->EncodeHandle(value->TakeChannel().release(),
                          {
                              .obj_type = Constraint::obj_type,
                              .rights = Constraint::rights,
                          },
                          position, Constraint::is_optional);
  }

  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    decoder->DecodeHandle(position,
                          {
                              .obj_type = Constraint::obj_type,
                              .rights = Constraint::rights,
                          },
                          Constraint::is_optional);
  }
};
#endif  // __Fuchsia__

struct TupleVisitor {
 public:
  // Returns true iff the |func| is satisfied on all items of |tuple|.
  //
  // e.g. TupleVisitor::All(std::make_tuple(1, 2, 3), [](int v) { return v > 0; })
  // returns true because 1, 2, 3 are all > 0.
  template <typename Tuple, typename Fn, size_t I = 0>
  static constexpr auto All(Tuple tuple, Fn func) {
    if constexpr (I == std::tuple_size_v<Tuple>) {
      return true;
    } else {
      return func(std::get<I>(tuple)) && All<Tuple, Fn, I + 1>(tuple, func);
    }
  }
};

template <typename Field_, typename Constraint_, bool IsRecursive>
struct WireStructMemberCodingInfo {
  using Field = Field_;
  using Constraint = Constraint_;
  static constexpr bool kIsRecursive = IsRecursive;
};

// In order to prevent cyclic dependency issues computing is_memcpy_compatible, perform the
// computation in a base class.
template <typename T, typename Constraint, bool IsRecursive>
struct WireStructCodingTraitsBase {
  static constexpr bool are_members_memcpy_compatible = TupleVisitor::All(
      WireCodingTraits<T, Constraint, IsRecursive>::kMembers, [](auto coding_info) {
        return WireCodingTraits<typename decltype(coding_info)::Field,
                                typename decltype(coding_info)::Constraint,
                                decltype(coding_info)::kIsRecursive>::is_memcpy_compatible;
      });
  static constexpr bool is_memcpy_compatible =
      are_members_memcpy_compatible && !WireCodingTraits<T, Constraint, IsRecursive>::kHasPadding;
};

template <bool IsRecursive>
struct WireTableCodingTraitsBase {
  enum class PreworkResult : bool {
    kSuccess,
    kEarlyExit,
  };

  static PreworkResult PrepareForBodyEncode(internal::WireEncoder* encoder, void* value,
                                            WirePosition position,
                                            WirePosition* out_vector_position) {
    fidl_vector_t* vec = static_cast<fidl_vector_t*>(value);

    *position.As<fidl_vector_t>() = {
        .count = vec->count,
        .data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT),
    };

    if (unlikely(vec->count == 0)) {
      return PreworkResult::kEarlyExit;
    }

    if (unlikely(!encoder->Alloc(sizeof(fidl_envelope_v2_t) * vec->count, out_vector_position))) {
      return PreworkResult::kEarlyExit;
    }
    return PreworkResult::kSuccess;
  }

  static PreworkResult DecodePrework(internal::WireDecoder* decoder, WirePosition position,
                                     WirePosition* out_vector_position) {
    fidl_vector_t* vec = position.As<fidl_vector_t>();
    if (unlikely(vec->data != reinterpret_cast<void*>(FIDL_ALLOC_PRESENT))) {
      decoder->SetError(kCodingErrorNullDataReceivedForTable);
      return PreworkResult::kEarlyExit;
    }

    if (unlikely(vec->count > std::numeric_limits<uint32_t>::max())) {
      decoder->SetError(kCodingErrorAllocationSizeExceeds32Bits);
      return PreworkResult::kEarlyExit;
    }
    if (unlikely(!decoder->Alloc(sizeof(fidl_envelope_v2_t) * vec->count, out_vector_position))) {
      return PreworkResult::kEarlyExit;
    }
    vec->data = out_vector_position->As<void>();
    return PreworkResult::kSuccess;
  }
};

template <bool IsRecursive>
using EncodeFn = void (*)(WireEncoder*, void*, WirePosition, RecursionDepth<IsRecursive>);
template <bool IsRecursive>
using DecodeFn = void (*)(WireDecoder*, WirePosition, RecursionDepth<IsRecursive>);

template <typename T, typename Constraint, bool IsRecursive>
constexpr EncodeFn<IsRecursive> MakeEncodeFn() {
  return [](WireEncoder* encoder, void* value, WirePosition position,
            RecursionDepth<IsRecursive> recursion_depth) {
    return WireCodingTraits<T, Constraint, IsRecursive>::Encode(encoder, static_cast<T*>(value),
                                                                position, recursion_depth);
  };
}
template <typename T, typename Constraint, bool IsRecursive>
constexpr DecodeFn<IsRecursive> MakeDecodeFn() {
  return WireCodingTraits<T, Constraint, IsRecursive>::Decode;
}

void WireDecodeUnknownEnvelope(WireDecoder* decoder, WirePosition position);

template <bool IsRecursive>
void WireEncodeEnvelope(size_t inline_size, EncodeFn<IsRecursive> encode_fn, WireEncoder* encoder,
                        fidl_envelope_v2_t* value, WirePosition position,
                        RecursionDepth<IsRecursive> recursion_depth) {
  if (!encode_fn || *reinterpret_cast<void**>(value) == nullptr) {
    // Unset or unknown envelope.
    *position.As<fidl_envelope_v2_t>() = {};
    return;
  }

  const size_t length_before = encoder->CurrentLength();
  const size_t handles_before = encoder->CurrentHandleCount();

  if (inline_size <= FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
    // Zero inline bytes (in case of padding).
    *position.As<uint32_t>() = 0;

    encode_fn(encoder, value->inline_value, position, recursion_depth);

    fidl_envelope_v2_t* envelope = position.As<fidl_envelope_v2_t>();
    envelope->num_handles = static_cast<uint16_t>(encoder->CurrentHandleCount() - handles_before);
    envelope->flags = FIDL_ENVELOPE_FLAGS_INLINING_MASK;
    return;
  }

  WirePosition body;
  if (unlikely(!encoder->Alloc(inline_size, &body))) {
    return;
  }
  encode_fn(encoder, *reinterpret_cast<void**>(value), body, recursion_depth);

  fidl_envelope_v2_t* envelope = position.As<fidl_envelope_v2_t>();
  envelope->num_bytes = static_cast<uint32_t>(encoder->CurrentLength() - length_before);
  envelope->num_handles = static_cast<uint16_t>(encoder->CurrentHandleCount() - handles_before);
  envelope->flags = 0;
}

template <bool IsRecursive>
void WireDecodeEnvelope(size_t inline_size, DecodeFn<IsRecursive> decode_fn, WireDecoder* decoder,
                        WirePosition position, RecursionDepth<IsRecursive> recursion_depth) {
  ZX_DEBUG_ASSERT(decode_fn != nullptr);

  const size_t length_before = decoder->CurrentLength();
  const size_t handles_before = decoder->CurrentHandleCount();

  fidl_envelope_v2_t envelope = *position.As<fidl_envelope_v2_t>();
  if (inline_size <= FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
    if (unlikely(envelope.flags != FIDL_ENVELOPE_FLAGS_INLINING_MASK)) {
      decoder->SetError(kCodingErrorInvalidInlineBit);
      return;
    }

    decode_fn(decoder, position, recursion_depth);

    if (unlikely(decoder->CurrentHandleCount() != handles_before + envelope.num_handles)) {
      decoder->SetError(kCodingErrorInvalidNumHandlesSpecifiedInEnvelope);
      return;
    }

    uint32_t padding;
    switch (inline_size) {
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
    if (unlikely((*position.As<uint32_t>() & padding) != 0)) {
      decoder->SetError(kCodingErrorInvalidPaddingBytes);
    }
    return;
  }

  if (unlikely(envelope.flags != 0)) {
    decoder->SetError(kCodingErrorInvalidInlineBit);
    return;
  }

  WirePosition body;
  if (unlikely(!decoder->Alloc(inline_size, &body))) {
    return;
  }
  *position.As<void*>() = body.As<void>();
  decode_fn(decoder, body, recursion_depth);

  if (unlikely(decoder->CurrentHandleCount() != handles_before + envelope.num_handles)) {
    decoder->SetError(kCodingErrorInvalidNumHandlesSpecifiedInEnvelope);
    return;
  }
  if (unlikely(decoder->CurrentLength() != length_before + envelope.num_bytes)) {
    decoder->SetError(kCodingErrorInvalidNumBytesSpecifiedInEnvelope);
    return;
  }
}

template <bool IsRecursive>
void WireDecodeStrictEnvelope(size_t inline_size, DecodeFn<IsRecursive> decode_fn,
                              WireDecoder* decoder, WirePosition position,
                              RecursionDepth<IsRecursive> recursion_depth) {
  if (!decode_fn) {
    decoder->SetError(kCodingErrorUnknownEnumValue);
    return;
  }
  WireDecodeEnvelope(inline_size, decode_fn, decoder, position, recursion_depth);
}

template <bool IsRecursive>
void WireDecodeFlexibleEnvelope(size_t inline_size, DecodeFn<IsRecursive> decode_fn,
                                WireDecoder* decoder, WirePosition position,
                                RecursionDepth<IsRecursive> recursion_depth) {
  if (!decode_fn) {
    WireDecodeUnknownEnvelope(decoder, position);
    return;
  }
  WireDecodeEnvelope(inline_size, decode_fn, decoder, position, recursion_depth);
}

template <bool IsRecursive>
void WireDecodeOptionalEnvelope(size_t inline_size, DecodeFn<IsRecursive> decode_fn,
                                WireDecoder* decoder, WirePosition position,
                                RecursionDepth<IsRecursive> recursion_depth) {
  static_assert(sizeof(fidl_envelope_v2_t) == sizeof(uint64_t));
  if (unlikely(*position.As<uint64_t>() == 0)) {
    return;
  }
  WireDecodeFlexibleEnvelope(inline_size, decode_fn, decoder, position, recursion_depth);
}

template <typename T>
struct WireIsRecursive {
  // Values are considered to be recursive if the values are not bounded to the recursion
  // depth.
  // The purpose of this is to statically enable / disable recursion checks which when
  // enabled have negative code size / performance consequences.
  static constexpr bool value = TypeTraits<T>::kMaxDepth > kWireRecursionDepthMax;
};

// Coding traits to use for the outermost value being encoded.
template <typename T>
using TopLevelCodingTraits =
    WireCodingTraits<T, WireCodingConstraintEmpty, WireIsRecursive<T>::value>;

using TopLevelEncodeFn = void (*)(WireEncoder*, void*, WirePosition);
using TopLevelDecodeFn = void (*)(WireDecoder*, WirePosition);

template <typename T>
constexpr TopLevelEncodeFn MakeTopLevelEncodeFn() {
  return [](WireEncoder* encoder, void* value, WirePosition position) {
    TopLevelCodingTraits<T>::Encode(encoder, static_cast<T*>(value), position,
                                    RecursionDepth<WireIsRecursive<T>::value>::Initial());
  };
}
template <typename T>
constexpr TopLevelDecodeFn MakeTopLevelDecodeFn() {
  return [](WireDecoder* decoder, WirePosition position) {
    TopLevelCodingTraits<T>::Decode(decoder, position,
                                    RecursionDepth<WireIsRecursive<T>::value>::Initial());
  };
}

// |kNullCodingConfig| may be used when an incoming message is all bytes and
// does not have any handle.
extern const CodingConfig kNullCodingConfig;

// Create a fidl::WireEncoder and encode the inputted value.
// This is the top-level function to call to perform encoding using coding traits.
fit::result<fidl::Error, WireEncoder::Result> WireEncode(
    size_t inline_size, TopLevelEncodeFn encode_fn, const CodingConfig* coding_config, void* value,
    zx_channel_iovec_t* iovecs, size_t iovec_capacity, fidl_handle_t* handles,
    fidl_handle_metadata_t* handle_metadata, size_t handle_capacity, uint8_t* backing_buffer,
    size_t backing_buffer_capacity);

// Create a |fidl::WireDecoder| and decode the inputted |message|.
// In case of error, handles in |message| are consumed.
// In case of success, handle values will be embedded in the decoded bytes; the caller
// needs to adopt them into a |DecodedValue|.
// This is the top-level function to call to perform decoding using coding traits.
fidl::Status WireDecode(::fidl::WireFormatMetadata metadata, bool contains_envelope,
                        size_t inline_size, TopLevelDecodeFn decode_fn,
                        ::fidl::EncodedMessage& message);

}  // namespace fidl::internal

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_CODING_TRAITS_H_
