// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_WIRE_NATURAL_CONVERSIONS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_WIRE_NATURAL_CONVERSIONS_H_

#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/fidl/llcpp/vector_view.h>

namespace fidl {
namespace internal {

template <typename WireType, typename NaturalType>
struct WireNaturalConversionTraits;

template <typename T>
struct WireNaturalConversionTraits<T, T> {
  static T ToNatural(T src) { return std::move(src); }
};

template <typename WireType>
struct NaturalTypeForWireType {
  using type = WireType;
};

template <>
struct WireNaturalConversionTraits<fidl::StringView, std::string> {
  static std::string ToNatural(fidl::StringView src) { return std::string(src.data(), src.size()); }
};

template <>
struct WireNaturalConversionTraits<fidl::StringView, cpp17::optional<std::string>> {
  static cpp17::optional<std::string> ToNatural(fidl::StringView src) {
    if (!src.data()) {
      return cpp17::nullopt;
    }
    return WireNaturalConversionTraits<fidl::StringView, std::string>::ToNatural(src);
  }
};

template <>
struct NaturalTypeForWireType<fidl::StringView> {
  using type = cpp17::optional<std::string>;
};

template <typename WireType, typename NaturalType>
struct WireNaturalConversionTraits<fidl::VectorView<WireType>, std::vector<NaturalType>> {
  static std::vector<NaturalType> ToNatural(fidl::VectorView<WireType> src) {
    std::vector<NaturalType> vec;
    vec.reserve(src.count());
    for (uint32_t i = 0; i < src.count(); i++) {
      vec.push_back(
          WireNaturalConversionTraits<WireType, NaturalType>::ToNatural(std::move(src[i])));
    }
    return vec;
  }
};

template <typename WireType, typename NaturalType>
struct WireNaturalConversionTraits<fidl::VectorView<WireType>,
                                   cpp17::optional<std::vector<NaturalType>>> {
  static cpp17::optional<std::vector<NaturalType>> ToNatural(fidl::VectorView<WireType> src) {
    if (!src.data()) {
      return cpp17::nullopt;
    }
    return WireNaturalConversionTraits<fidl::VectorView<WireType>,
                                       std::vector<NaturalType>>::ToNatural(src);
  }
};

template <typename WireType>
struct NaturalTypeForWireType<fidl::VectorView<WireType>> {
  using type = cpp17::optional<std::vector<typename NaturalTypeForWireType<WireType>::type>>;
};

template <typename WireType, typename NaturalType, size_t N>
struct WireNaturalConversionTraits<fidl::Array<WireType, N>, std::array<NaturalType, N>> {
  static std::array<NaturalType, N> ToNatural(fidl::Array<WireType, N> src) {
    return ArrayToNaturalHelper(std::move(src), std::make_index_sequence<N>());
  }
  template <std::size_t... Indexes>
  static std::array<NaturalType, N> ArrayToNaturalHelper(fidl::Array<WireType, N> value,
                                                         std::index_sequence<Indexes...>) {
    return std::array<NaturalType, N>{WireNaturalConversionTraits<WireType, NaturalType>::ToNatural(
        std::move(value[Indexes]))...};
  }
};

template <typename WireType, size_t N>
struct NaturalTypeForWireType<fidl::Array<WireType, N>> {
  using type = std::array<typename NaturalTypeForWireType<WireType>::type, N>;
};

template <typename WireType, typename NaturalType>
struct WireNaturalConversionTraits<fidl::ObjectView<WireType>, std::unique_ptr<NaturalType>> {
  static std::unique_ptr<NaturalType> ToNatural(fidl::ObjectView<WireType> src) {
    if (!src) {
      return nullptr;
    }
    return std::make_unique<NaturalType>(
        WireNaturalConversionTraits<WireType, NaturalType>::ToNatural(*src));
  }
};

template <typename WireType>
struct NaturalTypeForWireType<fidl::ObjectView<WireType>> {
  using type = std::unique_ptr<typename NaturalTypeForWireType<WireType>::type>;
};

template <typename WireTopResponseType, typename NaturalErrorType, typename NaturalValueType>
struct WireNaturalConversionTraits<WireTopResponseType,
                                   fitx::result<NaturalErrorType, NaturalValueType>> {
  static fitx::result<NaturalErrorType, NaturalValueType> ToNatural(WireTopResponseType src) {
    if (src.result.is_err()) {
      using WireErrorType = std::remove_reference_t<decltype(src.result.err())>;
      return fitx::error<NaturalErrorType>(
          WireNaturalConversionTraits<WireErrorType, NaturalErrorType>::ToNatural(
              std::move(src.result.err())));
    }
    using WireValueType = std::remove_reference_t<decltype(src.result.response())>;
    return fitx::ok<NaturalValueType>(
        WireNaturalConversionTraits<WireValueType, NaturalValueType>::ToNatural(
            std::move(src.result.response())));
  }
};

template <typename WireTopResponseType, typename NaturalErrorType>
struct WireNaturalConversionTraits<WireTopResponseType, fitx::result<NaturalErrorType>> {
  static fitx::result<NaturalErrorType> ToNatural(WireTopResponseType src) {
    if (src.result.is_err()) {
      using WireErrorType = std::remove_reference_t<decltype(src.result.err())>;
      return fitx::error<NaturalErrorType>(
          WireNaturalConversionTraits<WireErrorType, NaturalErrorType>::ToNatural(
              std::move(src.result.err())));
    }
    return fitx::ok();
  }
};

template <typename NaturalType, typename WireType>
NaturalType ToNatural(WireType value) {
  return internal::WireNaturalConversionTraits<WireType, NaturalType>::ToNatural(std::move(value));
}

}  // namespace internal

// fidl::ToNatural(wire_value) -> natural_value
//
// ToNatural is a converter from wire types to natural types.
// ToNatural will succeed so long as the input data is valid (e.g. no bad pointers).
// In cases where the natural type is ambiguous due to optionality, the optional version
// of the type will be returned.
template <typename WireType>
auto ToNatural(WireType value) {
  return internal::ToNatural<typename internal::NaturalTypeForWireType<WireType>::type, WireType>(
      std::move(value));
}

}  // namespace fidl

#endif
