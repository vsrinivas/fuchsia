// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_WIRE_NATURAL_CONVERSIONS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_WIRE_NATURAL_CONVERSIONS_H_

#include <lib/fidl/cpp/wire/object_view.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fidl/cpp/wire/vector_view.h>

#include <type_traits>

namespace fidl {
namespace internal {

// The base case for |NaturalTypeForWireType| prints an error.
// Generated code should specialize this template for generated wire types.
template <typename WireType, typename Enable = void>
struct NaturalTypeForWireType {
  // A type-dependent expression to help emit a static_assert error only when we
  // hit this template specialization base case. `static_assert(false);` does
  // not work because the compiler will eagerly fail. |T| is not used otherwise.
  template <typename... T>
  static constexpr bool kDoesNotKnowHowToConvertThisType = false;
  static_assert(kDoesNotKnowHowToConvertThisType<WireType>,
                "Does not know how to convert this type to its corresponding natural type. "
                "Make sure to include the FIDL headers for natural types. "
                "The #include pattern looks like #include <fidl/my.library/cpp/fidl.h>");
  using type = WireType;
};

// The base case for |WireTypeForNaturalType| prints an error.
// Generated code should specialize this template for generated natural types.
template <typename NaturalType, typename Enable = void>
struct WireTypeForNaturalType {
  // A type-dependent expression to help emit a static_assert error only when we
  // hit this template specialization base case. `static_assert(false);` does
  // not work because the compiler will eagerly fail. |T| is not used otherwise.
  template <typename... T>
  static constexpr bool kDoesNotKnowHowToConvertThisType = false;
  static_assert(kDoesNotKnowHowToConvertThisType<NaturalType>,
                "Does not know how to convert this type to its corresponding wire type. "
                "Make sure to include the FIDL headers for natural types. "
                "The #include pattern looks like #include <fidl/my.library/cpp/fidl.h>");
  using type = NaturalType;
};

// Specialize for primitive types.
template <typename T>
struct NaturalTypeForWireType<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
  using type = T;
};
template <typename T>
struct WireTypeForNaturalType<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
  using type = T;
};

template <typename WireType, typename NaturalType>
struct WireNaturalConversionTraits;

template <typename T>
struct WireNaturalConversionTraits<T, T> {
  static T ToNatural(T src) { return std::move(src); }
  static T ToWire(fidl::AnyArena&, T src) { return std::move(src); }
};

template <>
struct WireNaturalConversionTraits<fidl::StringView, std::string> {
  static std::string ToNatural(fidl::StringView src) { return std::string(src.data(), src.size()); }
  static fidl::StringView ToWire(fidl::AnyArena& arena, std::string src) {
    return fidl::StringView(arena, src);
  }
};

template <>
struct WireNaturalConversionTraits<fidl::StringView, std::optional<std::string>> {
  static std::optional<std::string> ToNatural(fidl::StringView src) {
    if (!src.data()) {
      return std::nullopt;
    }
    return WireNaturalConversionTraits<fidl::StringView, std::string>::ToNatural(src);
  }
  static fidl::StringView ToWire(fidl::AnyArena& arena, std::optional<std::string> src) {
    if (!src.has_value()) {
      return fidl::StringView::FromExternal(nullptr, 0);
    }
    return WireNaturalConversionTraits<fidl::StringView, std::string>::ToWire(arena, src.value());
  }
};

template <>
struct NaturalTypeForWireType<fidl::StringView> {
  using type = std::optional<std::string>;
};
template <>
struct WireTypeForNaturalType<std::string> {
  using type = fidl::StringView;
};
template <>
struct WireTypeForNaturalType<std::optional<std::string>> {
  using type = fidl::StringView;
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
    return std::move(vec);
  }
  static fidl::VectorView<WireType> ToWire(fidl::AnyArena& arena, std::vector<NaturalType> src) {
    fidl::VectorView<WireType> vec(arena, src.size());
    for (uint32_t i = 0; i < src.size(); i++) {
      vec[i] = WireNaturalConversionTraits<WireType, NaturalType>::ToWire(arena, std::move(src[i]));
    }
    return std::move(vec);
  }
};

template <typename WireType, typename NaturalType>
struct WireNaturalConversionTraits<fidl::VectorView<WireType>,
                                   std::optional<std::vector<NaturalType>>> {
  static std::optional<std::vector<NaturalType>> ToNatural(fidl::VectorView<WireType> src) {
    if (!src.data()) {
      return std::nullopt;
    }
    return WireNaturalConversionTraits<fidl::VectorView<WireType>,
                                       std::vector<NaturalType>>::ToNatural(src);
  }
  static fidl::VectorView<WireType> ToWire(fidl::AnyArena& arena,
                                           std::optional<std::vector<NaturalType>> src) {
    if (!src.has_value()) {
      return fidl::VectorView<WireType>();
    }
    return WireNaturalConversionTraits<fidl::VectorView<WireType>,
                                       std::vector<NaturalType>>::ToWire(arena,
                                                                         std::move(src.value()));
  }
};

template <typename WireType>
struct NaturalTypeForWireType<fidl::VectorView<WireType>> {
  using type = std::optional<std::vector<typename NaturalTypeForWireType<WireType>::type>>;
};
template <typename NaturalType>
struct WireTypeForNaturalType<std::vector<NaturalType>> {
  using type = fidl::VectorView<typename WireTypeForNaturalType<NaturalType>::type>;
};
template <typename NaturalType>
struct WireTypeForNaturalType<std::optional<std::vector<NaturalType>>> {
  using type = fidl::VectorView<typename WireTypeForNaturalType<NaturalType>::type>;
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

  static fidl::Array<WireType, N> ToWire(fidl::AnyArena& arena, std::array<NaturalType, N> src) {
    return ArrayToWireHelper(arena, std::move(src), std::make_index_sequence<N>());
  }
  template <std::size_t... Indexes>
  static fidl::Array<WireType, N> ArrayToWireHelper(fidl::AnyArena& arena,
                                                    std::array<NaturalType, N> value,
                                                    std::index_sequence<Indexes...>) {
    return fidl::Array<WireType, N>{WireNaturalConversionTraits<WireType, NaturalType>::ToWire(
        arena, std::move(value[Indexes]))...};
  }
};

template <typename WireType, size_t N>
struct NaturalTypeForWireType<fidl::Array<WireType, N>> {
  using type = std::array<typename NaturalTypeForWireType<WireType>::type, N>;
};
template <typename NaturalType, size_t N>
struct WireTypeForNaturalType<std::array<NaturalType, N>> {
  using type = fidl::Array<typename WireTypeForNaturalType<NaturalType>::type, N>;
};

template <typename WireType, typename NaturalType>
struct WireNaturalConversionTraits<fidl::ObjectView<WireType>, std::unique_ptr<NaturalType>> {
  static std::unique_ptr<NaturalType> ToNatural(fidl::ObjectView<WireType> src) {
    if (!src) {
      return nullptr;
    }
    return std::make_unique<NaturalType>(
        WireNaturalConversionTraits<WireType, NaturalType>::ToNatural(std::move(*src)));
  }
  static fidl::ObjectView<WireType> ToWire(fidl::AnyArena& arena,
                                           std::unique_ptr<NaturalType> src) {
    if (!src) {
      return nullptr;
    }
    return fidl::ObjectView<WireType>(
        arena, WireNaturalConversionTraits<WireType, NaturalType>::ToWire(arena, std::move(*src)));
  }
};

template <typename WireType>
struct NaturalTypeForWireType<fidl::ObjectView<WireType>> {
  using type = std::unique_ptr<typename NaturalTypeForWireType<WireType>::type>;
};
template <typename NaturalType>
struct WireTypeForNaturalType<std::unique_ptr<NaturalType>> {
  using type = fidl::ObjectView<typename WireTypeForNaturalType<NaturalType>::type>;
};

template <typename WireTopResponseType, typename NaturalErrorType, typename NaturalValueType>
struct WireNaturalConversionTraits<WireTopResponseType,
                                   fit::result<NaturalErrorType, NaturalValueType>> {
  static fit::result<NaturalErrorType, NaturalValueType> ToNatural(WireTopResponseType src) {
    if (src.result.is_err()) {
      using WireErrorType = std::remove_reference_t<decltype(src.result.err())>;
      return fit::error<NaturalErrorType>(
          WireNaturalConversionTraits<WireErrorType, NaturalErrorType>::ToNatural(
              std::move(src.result.err())));
    }
    using WireValueType = std::remove_reference_t<decltype(src.result.response())>;
    return fit::ok<NaturalValueType>(
        WireNaturalConversionTraits<WireValueType, NaturalValueType>::ToNatural(
            std::move(src.result.response())));
  }
  static WireTopResponseType ToWire(fidl::AnyArena& arena,
                                    fit::result<NaturalErrorType, NaturalValueType> src) {
    if (src.is_error()) {
      return WireTopResponseType{
          .result = WireTopResponseType::Result::WithErr(
              WireNaturalConversionTraits<typename WireTypeForNaturalType<NaturalErrorType>::type,
                                          NaturalErrorType>::ToWire(arena,
                                                                    std::move(src.error_value()))),
      };
    }
    if constexpr (sizeof(typename WireTypeForNaturalType<NaturalValueType>::type) <=
                  FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
      return WireTopResponseType{
          .result = WireTopResponseType::Result::WithResponse(
              WireNaturalConversionTraits<typename WireTypeForNaturalType<NaturalValueType>::type,
                                          NaturalValueType>::ToWire(arena, std::move(src.value()))),
      };
    } else {
      return WireTopResponseType{
          .result = WireTopResponseType::Result::WithResponse(
              arena,
              WireNaturalConversionTraits<typename WireTypeForNaturalType<NaturalValueType>::type,
                                          NaturalValueType>::ToWire(arena, std::move(src.value()))),
      };
    }
  }
};

template <typename WireTopResponseType, typename NaturalErrorType>
struct WireNaturalConversionTraits<WireTopResponseType, fit::result<NaturalErrorType>> {
  static fit::result<NaturalErrorType> ToNatural(WireTopResponseType src) {
    if (src.result.is_err()) {
      using WireErrorType = std::remove_reference_t<decltype(src.result.err())>;
      return fit::error<NaturalErrorType>(
          WireNaturalConversionTraits<WireErrorType, NaturalErrorType>::ToNatural(
              std::move(src.result.err())));
    }
    return fit::ok();
  }
  static WireTopResponseType ToWire(fidl::AnyArena& arena, fit::result<NaturalErrorType> src) {
    if (src.is_error()) {
      return WireTopResponseType{
          .result = WireTopResponseType::Result::WithErr(
              WireNaturalConversionTraits<typename WireTypeForNaturalType<NaturalErrorType>::type,
                                          NaturalErrorType>::ToWire(arena,
                                                                    std::move(src.error_value()))),
      };
    }
    return WireTopResponseType{
        .result = WireTopResponseType::Result::WithResponse({}),
    };
  }
};

template <typename NaturalType, typename WireType>
NaturalType ToNatural(WireType value) {
  return internal::WireNaturalConversionTraits<WireType, NaturalType>::ToNatural(std::move(value));
}

template <typename WireType, typename NaturalType>
WireType ToWire(fidl::AnyArena& arena, NaturalType value) {
  return internal::WireNaturalConversionTraits<WireType, NaturalType>::ToWire(arena,
                                                                              std::move(value));
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
  using DecayedWire = std::remove_cv_t<std::remove_reference_t<WireType>>;
  return internal::ToNatural<typename internal::NaturalTypeForWireType<DecayedWire>::type,
                             DecayedWire>(std::move(value));
}

// fidl::ToWire(natural_value, arena) -> wire_value
//
// ToWire is a converter from wire types to natural types.
// ToWire will succeed so long as the input data is valid (e.g. no bad pointers).
//
// All out-of-line values will be copied to |arena|.
template <typename NaturalType>
auto ToWire(fidl::AnyArena& arena, NaturalType value) {
  using DecayedNatural = std::remove_cv_t<std::remove_reference_t<NaturalType>>;
  return internal::ToWire<typename internal::WireTypeForNaturalType<DecayedNatural>::type,
                          DecayedNatural>(arena, std::move(value));
}

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_WIRE_NATURAL_CONVERSIONS_H_
