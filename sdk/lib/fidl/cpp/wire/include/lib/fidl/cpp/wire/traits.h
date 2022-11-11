// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_TRAITS_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_TRAITS_H_

#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/internal.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#endif

#include <cstdint>
#include <type_traits>

// Defines type traits used by both wire and natural types.
//
// The contracts of a FIDL type |T| are as follows:
//
// |IsFidlType<T>|    resolves to std::true_type.
// |IsFidlTransactionalMessage<T>| resolves to std::true_type iff |T| is a transactional message.
// |IsResource<T>|    resolves to std::true_type iff |T| is a resource type.
//
// |TypeTraits<T>| will be specialized for the type, see documentation below for
//                 what fields it will contain.

namespace fidl {
namespace internal {
// A type trait that indicates whether the given type is a primitive FIDL type.
template <typename T>
struct IsPrimitive : public std::false_type {};

// Const-ness is not significant for determining IsPrimitive.
template <typename T>
struct IsPrimitive<const T> : public IsPrimitive<T> {};

// clang-format off
template <> struct IsPrimitive<bool> : public std::true_type {};
template <> struct IsPrimitive<char> : public std::true_type {};
template <> struct IsPrimitive<uint8_t> : public std::true_type {};
template <> struct IsPrimitive<uint16_t> : public std::true_type {};
template <> struct IsPrimitive<uint32_t> : public std::true_type {};
template <> struct IsPrimitive<uint64_t> : public std::true_type {};
template <> struct IsPrimitive<int8_t> : public std::true_type {};
template <> struct IsPrimitive<int16_t> : public std::true_type {};
template <> struct IsPrimitive<int32_t> : public std::true_type {};
template <> struct IsPrimitive<int64_t> : public std::true_type {};
template <> struct IsPrimitive<float> : public std::true_type {};
template <> struct IsPrimitive<double> : public std::true_type {};
// clang-format on

template <typename Protocol>
struct ProtocolDetails;

}  // namespace internal

// A type trait that indicates whether the given type is a request/response type
// i.e. has a FIDL message header.
template <typename T>
struct IsFidlTransactionalMessage : public std::false_type {};

// Code-gen will explicitly conform the generated FIDL transactional messages to
// IsFidlTransactionalMessage.

// A type trait that indicates whether the given type is allowed to appear in
// generated binding APIs and can be encoded/decoded.
// As a start, all handle types are supported.
#ifdef __Fuchsia__
template <typename T>
struct IsFidlType : public std::is_base_of<zx::object_base, T> {};
#else
template <typename T>
struct IsFidlType : public std::false_type {};
#endif

// Const-ness is not significant for determining IsFidlType.
template <typename T>
struct IsFidlType<const T> : public IsFidlType<T> {};

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

// A type trait that indicates whether the given type is a resource type
// i.e. can contain handles.
#ifdef __Fuchsia__
template <typename T>
struct IsResource : public std::is_base_of<zx::object_base, T> {
  static_assert(IsFidlType<T>::value, "IsResource only defined on FIDL types.");
};
#else
template <typename T>
struct IsResource : public std::false_type {
  static_assert(IsFidlType<T>::value, "IsResource only defined on FIDL types.");
};
#endif
// Code-gen will explicitly conform the generated FIDL types to IsResource.

// A type trait that contains several properties of FIDL types which are
// important for encoding/decoding.
//
// |kMaxNumHandles|       is a uint32_t specifying the upper bound on the number
//                        of contained handles.
// |kMaxDepth|            is a uint32_t specifying the upper bound on the object
//                        nesting depth.
// |kPrimarySize|         is a uint32_t specifying the size in bytes of the
//                        inline part of the message.
// |kMaxOutOfLine|        is a uint32_t specifying the upper bound on the
//                        out-of-line message size.  It is
//                        std::numeric_limits<uint32_t>::max() if |T| is
//                        unbounded.
// |kHasFlexibleEnvelope| is a bool specifying if this message contains a
//                        flexible union or a flexible table.
// |kHasEnvelope|         is a boolean specifying if the structure contains
//                        envelopes.
// |kHasPointer|          is a boolean specifying if the structure contains
//                        pointer indirections, hence requires linearization
//                        when sending.
//
// Additionally, if |T| is a transactional message:
//
// |kMessageKind|         identifies if this message is a request or a response.
//                        If undefined, the type may be used either as a request
//                        or a response.
template <typename T>
struct TypeTraits {};
// Code-gen will explicitly conform the generated FIDL types to TypeTraits.

// Const-ness is not significant for determining TypeTraits.
template <typename T>
struct TypeTraits<const T> : public TypeTraits<T> {};

template <typename T>
struct DeprecatedCTypeTraits;

// String
class StringView;
template <>
struct IsFidlType<StringView> : public std::true_type {};

template <typename T, typename Enable = void>
struct IsStringView : std::false_type {};
template <>
struct IsStringView<fidl::StringView, void> : std::true_type {};
template <typename T>
struct IsStringView<
    T, typename std::enable_if<IsStringView<typename std::remove_const<T>::type>::value>::type>
    : std::true_type {};

// Vector (conditional on element)
template <typename E>
class VectorView;
template <typename E>
struct IsFidlType<VectorView<E>> : public IsFidlType<E> {};

template <typename T, typename Enable = void>
struct IsVectorView : std::false_type {};
template <typename E>
struct IsResource<VectorView<E>> : public IsResource<E> {};
template <typename T>
struct IsVectorView<VectorView<T>, void> : std::true_type {};
template <typename T>
struct IsVectorView<
    T, typename std::enable_if<IsVectorView<typename std::remove_const<T>::type>::value>::type>
    : std::true_type {};

// Code-gen is responsible for emitting specializations for these traits
template <typename T, typename Enable = void>
struct IsTable : std::false_type {};
template <typename MaybeConstTable>
struct IsTable<MaybeConstTable,
               typename std::enable_if<
                   std::is_const<MaybeConstTable>::value &&
                   IsTable<typename std::remove_const<MaybeConstTable>::type>::value>::type>
    : std::true_type {};
template <typename T, typename Enable = void>
struct IsUnion : std::false_type {};
template <typename MaybeConstUnion>
struct IsUnion<MaybeConstUnion,
               typename std::enable_if<
                   std::is_const<MaybeConstUnion>::value &&
                   IsUnion<typename std::remove_const<MaybeConstUnion>::type>::value>::type>
    : std::true_type {};
template <typename T, typename Enable = void>
struct IsStruct : std::false_type {};
template <typename MaybeConstStruct>
struct IsStruct<MaybeConstStruct,
                typename std::enable_if<
                    std::is_const<MaybeConstStruct>::value &&
                    IsStruct<typename std::remove_const<MaybeConstStruct>::type>::value>::type>
    : std::true_type {};

// IsFidlObject is a subset of IsFidlType referring to user defined aggregate types, i.e.
// tables, unions, and structs.
template <typename T, typename Enable = void>
struct IsFidlObject : std::false_type {};
template <typename T>
struct IsFidlObject<
    T, typename std::enable_if<IsTable<T>::value || IsUnion<T>::value || IsStruct<T>::value>::type>
    : std::true_type {};

// Indicates if the parameterized type contains a handle.
template <typename T, typename Enable = void>
struct ContainsHandle;

// clang-format off
// Specialize for primitives
template <> struct ContainsHandle<bool> : public std::false_type {};
template <> struct ContainsHandle<uint8_t> : public std::false_type {};
template <> struct ContainsHandle<uint16_t> : public std::false_type {};
template <> struct ContainsHandle<uint32_t> : public std::false_type {};
template <> struct ContainsHandle<uint64_t> : public std::false_type {};
template <> struct ContainsHandle<int8_t> : public std::false_type {};
template <> struct ContainsHandle<int16_t> : public std::false_type {};
template <> struct ContainsHandle<int32_t> : public std::false_type {};
template <> struct ContainsHandle<int64_t> : public std::false_type {};
template <> struct ContainsHandle<float> : public std::false_type {};
template <> struct ContainsHandle<double> : public std::false_type {};
// clang-format on

#if __Fuchsia__
template <typename T>
struct ContainsHandle<T, typename std::enable_if<std::is_base_of<zx::object_base, T>::value>::type>
    : std::true_type {};
#endif

template <typename Protocol>
class ClientEnd;
template <typename Protocol>
class UnownedClientEnd;
template <typename Protocol>
class ServerEnd;

template <typename Protocol>
struct IsResource<ClientEnd<Protocol>> : public std::true_type {};

template <typename Protocol>
struct IsResource<ServerEnd<Protocol>> : public std::true_type {};

template <typename Protocol>
struct ContainsHandle<ClientEnd<Protocol>> : std::true_type {};
template <typename Protocol>
struct ContainsHandle<ServerEnd<Protocol>> : std::true_type {};

template <typename T>
struct ContainsHandle<
    T, typename std::enable_if<IsFidlType<T>::value && TypeTraits<T>::kMaxNumHandles == 0>::type>
    : std::false_type {};

template <typename T>
struct ContainsHandle<
    T, typename std::enable_if<(IsFidlType<T>::value && TypeTraits<T>::kMaxNumHandles > 0)>::type>
    : std::true_type {};

template <typename T, size_t N>
struct Array;
template <typename T, size_t N>
struct ContainsHandle<Array<T, N>> : ContainsHandle<T> {};
template <typename T, size_t N>
struct IsResource<Array<T, N>> : public IsResource<T> {};

template <typename T, typename = void>
struct IsProtocol : public ::std::false_type {};
template <typename T>
struct IsProtocol<T, std::void_t<decltype(::fidl::internal::ProtocolDetails<T>::kIsProtocol)>>
    : public std::true_type {};

template <typename T>
constexpr inline auto IsProtocolV = IsProtocol<T>::value;

template <typename T, typename = void>
struct IsService : public ::std::false_type {};
template <typename T>
struct IsService<T, std::void_t<decltype(T::kIsService)>> : public std::true_type {};

template <typename T>
constexpr inline auto IsServiceV = IsService<T>::value;

template <typename T, typename = void>
struct IsServiceMember : public ::std::false_type {};
template <typename T>
struct IsServiceMember<T, std::void_t<decltype(T::kIsServiceMember)>> : public std::true_type {};

template <typename T>
constexpr inline auto IsServiceMemberV = IsServiceMember<T>::value;

// Code-gen will explicitly conform the generated FIDL structures to IsFidlType.

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
struct IsResponseType<FidlType, void_t<decltype(TypeTraits<FidlType>::kMessageKind)>>
    : std::integral_constant<bool, TypeTraits<FidlType>::kMessageKind ==
                                       TransactionalMessageKind::kResponse> {};

// Calculates the maximum possible message size for a FIDL type,
// clamped at the Zircon channel transport packet size.
template <typename FidlType, const MessageDirection Direction>
constexpr uint32_t ClampedMessageSize() {
  static_assert(IsFidlType<FidlType>::value, "Only FIDL types allowed here");
  if constexpr (Direction == MessageDirection::kReceiving) {
    if (TypeTraits<FidlType>::kHasFlexibleEnvelope) {
      return ZX_CHANNEL_MAX_MSG_BYTES;
    }
  }
  uint64_t primary = FidlAlign(TypeTraits<FidlType>::kPrimarySize);
  uint64_t out_of_line = FidlAlign(TypeTraits<FidlType>::kMaxOutOfLine);
  uint64_t sum = primary + out_of_line;
  if (sum > ZX_CHANNEL_MAX_MSG_BYTES) {
    return ZX_CHANNEL_MAX_MSG_BYTES;
  } else {
    return static_cast<uint32_t>(sum);
  }
}

// Calculates the maximum possible handle count for a FIDL type,
// clamped at the Zircon channel transport handle limit.
template <typename FidlType, const MessageDirection Direction>
constexpr uint32_t ClampedHandleCount() {
  static_assert(IsFidlType<FidlType>::value, "Only FIDL types allowed here");
  if constexpr (Direction == MessageDirection::kReceiving) {
    if (TypeTraits<FidlType>::kHasFlexibleEnvelope) {
      return ZX_CHANNEL_MAX_MSG_HANDLES;
    }
  }
  uint32_t raw_max_handles = TypeTraits<FidlType>::kMaxNumHandles;
  if (raw_max_handles > ZX_CHANNEL_MAX_MSG_HANDLES) {
    return ZX_CHANNEL_MAX_MSG_HANDLES;
  } else {
    return raw_max_handles;
  }
}

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_TRAITS_H_
