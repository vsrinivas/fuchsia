// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_TRAITS_H_
#define LIB_FIDL_LLCPP_TRAITS_H_

#include <lib/fidl/internal.h>
#include <lib/zx/object.h>
#include <stdint.h>
#include <zircon/fidl.h>

#include <type_traits>

// Defines type traits used in the low-level C++ binding.
//
// The contracts of a FIDL type |T| are as follows:
//
// |IsFidlType<T>|    resolves to std::true_type.
// |IsFidlMessage<T>| resolves to std::true_type iff |T| is a transactional message.
// |T::MaxNumHandles| is a uint32_t specifying the upper bound on the number of contained handles.
// |T::PrimarySize|   is a uint32_t specifying the size in bytes of the inline part of the message.
// |T::MaxOutOfLine|  is a uint32_t specifying the upper bound on the out-of-line message size.
//                    It is std::numeric_limits<uint32_t>::max() if |T| is unbounded.
// |T::HasPointer|    is a boolean specifying if the structure contains pointer indirections, hence
//                    requires linearization when sending.
// |T::Type|          is a fidl_type_t* pointing to the corresponding coding table, if any.
//                    If the encoding/decoding of |T| can be elided, |T::Type| is NULL.
//
// Additionally, during wire-format migrations, the bindings may need to send/receive bytes in an
// alternate format different from the one implied by memory layouts of the generated structures:
//
// |T::AltType|         is a fidl_type_t* pointing to the coding table for the alternate format.
// |T::AltPrimarySize|  is the |PrimarySize| of the alternate format.
// |T::AltMaxOutOfLine| is the |MaxOutOfLine| of the alternate format.
//
// Additionally, if |T| is a transactional message:
//
// |T::HasFlexibleEnvelope| is a bool specifying if this message contains a flexible xunion or
//                          a flexible table.
// |T::ContainsUnion|       is a bool specifying if this message transitively contains any unions.
//                          If this false, transforming wire formats can be avoided
// |T::MessageKind|         identifies if this message is a request or a response. If undefined,
//                          the type may be used either as a request or a response.
//
// Additionally, if |T| is a non-empty request message of a FIDL transaction:
//
// |T::ResponseType| resolves to the corresponding response message type, if the FIDL method calls
//                   for a response. Otherwise, the definition does not exist.
//

namespace fidl {

// A type trait that indicates whether the given type is a request/response type
// i.e. has a FIDL message header.
template <typename T>
struct IsFidlMessage : public std::false_type {};

// Code-gen will explicitly conform the generated FIDL transactional messages to IsFidlMessage.

// A type trait that indicates whether the given type is allowed to appear in
// generated binding APIs and can be encoded/decoded.
// As a start, all handle types are supported.
template <typename T>
struct IsFidlType : public std::is_base_of<zx::object_base, T> {};

// clang-format off
// Specialize for primitives
template <> struct IsFidlType<bool> : public std::true_type {};
template <> struct IsFidlType<uint8_t> : public std::true_type {};
template <> struct IsFidlType<uint16_t> : public std::true_type {};
template <> struct IsFidlType<uint32_t> : public std::true_type {};
template <> struct IsFidlType<uint64_t> : public std::true_type {};
template <> struct IsFidlType<int8_t> : public std::true_type {};
template <> struct IsFidlType<int16_t> : public std::true_type {};
template <> struct IsFidlType<int32_t> : public std::true_type {};
template <> struct IsFidlType<int64_t> : public std::true_type {};
template <> struct IsFidlType<float> : public std::true_type {};
template <> struct IsFidlType<double> : public std::true_type {};
// clang-format on

// String
class StringView;
template <>
struct IsFidlType<StringView> : public std::true_type {};

// Vector (conditional on element)
template <typename E>
class VectorView;
template <typename E>
struct IsFidlType<VectorView<E>> : public IsFidlType<E> {};

// Code-gen will explicitly conform the generated FIDL structures to IsFidlType.

template <typename FidlType>
struct NeedsEncodeDecode {
  // A FIDL type with no coding table definition does not need any encoding/decoding,
  // as the in-memory representation of the type is identical to its on-wire representation.
  // Sometimes, GCC knows that the value can never equal nullptr and it may complain
  // that the comparison is always true. Just suppress the warning.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
  static constexpr bool value = FidlType::Type != nullptr;
#pragma GCC diagnostic pop
};

// The direction where a message is going.
// This has implications on the allocated buffer and handle size.
enum class MessageDirection {
  // Receiving the message from another end.
  kReceiving,

  // Sending the message to the other end.
  kSending
};

// Utilities used internally by the llcpp binding.
namespace internal {

// Whether a FIDL transactional message is used as a request or a response.
enum class TransactionalMessageKind {
  kRequest,
  kResponse,
};

// C++ 14 compatible implementation of std::void_t.
#if defined(__cplusplus) && __cplusplus >= 201703L
template <typename... T>
using void_t = std::void_t<T...>;
#else
template <typename... T>
struct make_void {
  typedef void type;
};
template <typename... T>
using void_t = typename make_void<T...>::type;
#endif

// IsResponseType<FidlType>() is true when FidlType is a FIDL response message type.
template <typename FidlType, typename = void_t<>>
struct IsResponseType : std::false_type {};
template <typename FidlType>
struct IsResponseType<FidlType, void_t<decltype(FidlType::MessageKind)>>
    : std::integral_constant<bool, FidlType::MessageKind == TransactionalMessageKind::kResponse> {};

// A type trait that indicates if the given FidlType is a request message type that also
// unambiguously declare a corresponding response message type.
template <typename, typename = void_t<>>
struct HasResponseType : std::false_type {};
template <typename FidlType>
struct HasResponseType<FidlType, void_t<typename FidlType::ResponseType>> : std::true_type {};

// This can be passed to the various message size calculation utilities to indicate which
// wire-format are we calculating for.
enum class WireFormatGuide {
  // The wire-format that corresponds to the generated LLCPP structures i.e. the in-memory format.
  kCurrent,

  // An alternate wire-format when a wire-format migration is ongoing.
  kAlternate
};

// Calculates the maximum possible message size for a FIDL type,
// clamped at the Zircon channel transport packet size.
// TODO(FIDL-771): users of this API should always specify a meaningful direction.
template <typename FidlType, const MessageDirection Direction,
          const WireFormatGuide WireFormat = WireFormatGuide::kCurrent>
constexpr uint32_t ClampedMessageSize() {
  static_assert(IsFidlType<FidlType>::value, "Only FIDL types allowed here");
  if constexpr (IsResponseType<FidlType>()) {
    if (FidlType::HasFlexibleEnvelope && Direction == MessageDirection::kReceiving) {
      return ZX_CHANNEL_MAX_MSG_BYTES;
    }
  }
  // These can be modified to return the ::Alt variant when a migration
  // is ongoing
  uint64_t primary = [] {
    switch (WireFormat) {
      case WireFormatGuide::kCurrent:
        return FidlAlign(FidlType::PrimarySize);
      case WireFormatGuide::kAlternate:
        return FidlAlign(FidlType::PrimarySize);
    }
  }();
  uint64_t out_of_line = [] {
    switch (WireFormat) {
      case WireFormatGuide::kCurrent:
        return FidlAlign(FidlType::MaxOutOfLine);
      case WireFormatGuide::kAlternate:
        return FidlAlign(FidlType::MaxOutOfLine);
    }
  }();
  uint64_t sum = primary + out_of_line;
  if (sum > ZX_CHANNEL_MAX_MSG_BYTES) {
    return ZX_CHANNEL_MAX_MSG_BYTES;
  } else {
    return static_cast<uint32_t>(sum);
  }
}

// Calculates the maximum possible handle count for a FIDL type,
// clamped at the Zircon channel transport handle limit.
// TODO(FIDL-771): users of this API should always specify a meaningful direction.
template <typename FidlType, const MessageDirection Direction>
constexpr uint32_t ClampedHandleCount() {
  static_assert(IsFidlType<FidlType>::value, "Only FIDL types allowed here");
  if constexpr (IsResponseType<FidlType>()) {
    if (FidlType::HasFlexibleEnvelope && Direction == MessageDirection::kReceiving) {
      return ZX_CHANNEL_MAX_MSG_HANDLES;
    }
  }
  uint32_t raw_max_handles = FidlType::MaxNumHandles;
  if (raw_max_handles > ZX_CHANNEL_MAX_MSG_HANDLES) {
    return ZX_CHANNEL_MAX_MSG_HANDLES;
  } else {
    return raw_max_handles;
  }
}

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_TRAITS_H_
