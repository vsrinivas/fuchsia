// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_HLCPP_CONVERSION_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_HLCPP_CONVERSION_H_

#include <lib/fidl/cpp/enum.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/wire/wire_types.h>

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#endif

namespace fidl {

class StringPtr;

namespace internal {

/* Helper trait to describe how a type in the natural bindings can be converted to a type in the
 * HLCPP bindings. Every specialization of this trait should have a `using HLCPPType` type alias to
 * indicate the type in the HLCPP bindings and a `static HLCPPType Convert(Natural&&)` method to do
 * the conversion.*/
template <typename Natural, typename = void>
struct NaturalToHLCPPTraits;

/* Helper trait to describe how a type in the HLCPP bindings can be converted to a type in the
 * natural bindings. Every specialization of this trait should have a `using NatuyralType` type
 * alias to indicate the type in the natural bindings and a `static NaturalType Convert(HLCPP&&)`
 * method to do the conversion.*/
template <typename HLCPP, typename = void>
struct HLCPPToNaturalTraits;

/* A template for NaturalToHLCPPTraits for types that are identical between natural and HLCPP
 * bindings. */
template <typename Natural>
struct NaturalToHLCPPTraitsIdentical {
  using HLCPPType = Natural;
  static inline HLCPPType Convert(Natural&& value) { return value; }
};

/* A template for HLCPPToNaturalTraits for types that are identical between natural and HLCPP
 * bindings. */
template <typename HLCPP>
struct HLCPPToNaturalTraitsIdentical {
  using NaturalType = HLCPP;
  static inline NaturalType Convert(HLCPP&& value) { return value; }
};

/* Natural to HLCPP traits for integer and floating point types. */
template <typename Natural>
struct NaturalToHLCPPTraits<
    Natural, std::enable_if_t<std::is_integral_v<Natural> || std::is_floating_point_v<Natural>>>
    final : public NaturalToHLCPPTraitsIdentical<Natural> {};

/* HLCPP to Natural traits for integer and floating point types. */
template <typename HLCPP>
struct HLCPPToNaturalTraits<
    HLCPP, std::enable_if_t<std::is_integral_v<HLCPP> || std::is_floating_point_v<HLCPP>>>
    final : public HLCPPToNaturalTraitsIdentical<HLCPP> {};

#ifdef __Fuchsia__
/* Natural to HLCPP types for handle types */
template <typename Natural>
struct NaturalToHLCPPTraits<Natural, std::enable_if_t<std::is_base_of_v<zx::object_base, Natural>>>
    final : public NaturalToHLCPPTraitsIdentical<Natural> {};

/* HLCPP to Natural types for handle types */
template <typename HLCPP>
struct HLCPPToNaturalTraits<HLCPP, std::enable_if_t<std::is_base_of_v<zx::object_base, HLCPP>>>
    final : public HLCPPToNaturalTraitsIdentical<HLCPP> {};
#endif

/* Natural to HLCPP traits for strings. */
template <>
struct NaturalToHLCPPTraits<std::string> final : public NaturalToHLCPPTraitsIdentical<std::string> {
};

/* HLCPP to Natural traits for strings. */
template <>
struct HLCPPToNaturalTraits<std::string> final : public HLCPPToNaturalTraitsIdentical<std::string> {
};

/* Natural to HLCPP traits for optional strings. */
template <>
struct NaturalToHLCPPTraits<std::optional<std::string>> final {
  using HLCPPType = ::fidl::StringPtr;
  static inline HLCPPType Convert(std::optional<std::string>&& natural) {
    if (natural.has_value()) {
      return ::fidl::StringPtr(std::move(natural.value()));
    }
    return ::fidl::StringPtr{};
  }
};

/* HLCPP to Natural traits for optional strings. */
template <>
struct HLCPPToNaturalTraits<fidl::StringPtr> final {
  using NaturalType = std::optional<std::string>;
  static inline NaturalType Convert(fidl::StringPtr&& hlcpp) {
    if (hlcpp.has_value()) {
      return std::make_optional(hlcpp.value());
    }
    return std::nullopt;
  }
};

// TODO(ianloic): special-case vectors of identical types - eg: std::vector<uint8_t>

/* Natural to HLCPP types traits for vectors */
template <typename NaturalMember>
struct NaturalToHLCPPTraits<std::vector<NaturalMember>> {
  using NaturalMemberTraits = NaturalToHLCPPTraits<NaturalMember>;
  using HLCPPType = std::vector<typename NaturalMemberTraits::HLCPPType>;
  static inline HLCPPType Convert(std::vector<NaturalMember>&& natural) {
    HLCPPType hlcpp;
    hlcpp.reserve(natural.size());
    for (auto&& m : natural) {
      hlcpp.push_back(NaturalMemberTraits::Convert(std::move(m)));
    }
    return hlcpp;
  }
};

/* HLCPP to Natural types traits for vectors */
template <typename HLCPPMember>
struct HLCPPToNaturalTraits<std::vector<HLCPPMember>> {
  using HLCPPMemberTraits = HLCPPToNaturalTraits<HLCPPMember>;
  using NaturalType = std::vector<typename HLCPPMemberTraits::NaturalType>;
  static inline NaturalType Convert(std::vector<HLCPPMember>&& hlcpp) {
    NaturalType natural;
    natural.reserve(hlcpp.size());
    for (auto&& m : hlcpp) {
      natural.push_back(HLCPPMemberTraits::Convert(std::move(m)));
    }
    return natural;
  }
};

/* Natural to HLCPP types traits for optional vectors */
template <typename NaturalMember>
struct NaturalToHLCPPTraits<std::optional<std::vector<NaturalMember>>> {
  using NaturalMemberTraits = NaturalToHLCPPTraits<NaturalMember>;
  using HLCPPType = ::fidl::VectorPtr<typename NaturalMemberTraits::HLCPPType>;
  static inline HLCPPType Convert(std::optional<std::vector<NaturalMember>>&& natural) {
    if (natural.has_value()) {
      std::vector<typename NaturalMemberTraits::HLCPPType> hlcpp;
      hlcpp.reserve(natural->size());
      for (auto&& m : natural.value()) {
        hlcpp.push_back(NaturalMemberTraits::Convert(std::move(m)));
      }
      return hlcpp;
    }
    return HLCPPType{};
  }
};

/* HLCPP to Natural types traits for optional vectors */
template <typename HLCPPMember>
struct HLCPPToNaturalTraits<::fidl::VectorPtr<HLCPPMember>> {
  using HLCPPMemberTraits = HLCPPToNaturalTraits<HLCPPMember>;
  using NaturalType = std::optional<std::vector<typename HLCPPMemberTraits::NaturalType>>;
  static inline NaturalType Convert(::fidl::VectorPtr<HLCPPMember>&& hlcpp) {
    if (hlcpp.has_value()) {
      std::vector<typename HLCPPMemberTraits::NaturalType> natural;
      natural.reserve(hlcpp->size());
      for (auto&& m : hlcpp.value()) {
        natural.push_back(HLCPPMemberTraits::Convert(std::move(m)));
      }
      return natural;
    }
    return std::nullopt;
  }
};

template <typename To, typename From, typename Func, std::size_t... Is>
std::array<To, sizeof...(Is)> ConvertArrayImpl(std::array<From, sizeof...(Is)> from, Func func,
                                               std::index_sequence<Is...>) {
  return {{func(std::move(from[Is]))...}};
}

template <typename To, typename From, std::size_t N, typename Func>
std::array<To, N> ConvertArray(std::array<From, N>&& from, Func func) {
  return ConvertArrayImpl<To>(std::move(from), func, std::make_index_sequence<N>());
}

/* Natural to HLCPP type traits for arrays */
template <typename NaturalMember, size_t N>
struct NaturalToHLCPPTraits<std::array<NaturalMember, N>> {
  using NaturalMemberTraits = NaturalToHLCPPTraits<NaturalMember>;
  using HLCPPType = std::array<typename NaturalMemberTraits::HLCPPType, N>;
  static inline HLCPPType Convert(std::array<NaturalMember, N>&& natural) {
    return ConvertArray<typename NaturalMemberTraits::HLCPPType>(std::move(natural),
                                                                 NaturalMemberTraits::Convert);
  }
};

/* HLCPP to Natural type traits for arrays */
template <typename HLCPPMember, size_t N>
struct HLCPPToNaturalTraits<std::array<HLCPPMember, N>> {
  using HLCPPMemberTraits = HLCPPToNaturalTraits<HLCPPMember>;
  using NaturalType = std::array<typename HLCPPMemberTraits::NaturalType, N>;
  static inline NaturalType Convert(std::array<HLCPPMember, N>&& hlcpp) {
    return ConvertArray<typename HLCPPMemberTraits::NaturalType>(std::move(hlcpp),
                                                                 HLCPPMemberTraits::Convert);
  }
};

/* Natural to HLCPP type traits for boxed types */
template <typename Natural>
struct NaturalToHLCPPTraits<std::unique_ptr<Natural>> {
  using NaturalInnerTraits = NaturalToHLCPPTraits<Natural>;
  using HLCPPType = std::unique_ptr<typename NaturalInnerTraits::HLCPPType>;
  static inline HLCPPType Convert(std::unique_ptr<Natural>&& natural) {
    if (natural) {
      return std::make_unique<typename NaturalInnerTraits::HLCPPType>(
          NaturalInnerTraits::Convert(std::move(*natural)));
    }
    return nullptr;
  }
};

/* HLCPP to Natural type traits for boxed types */
template <typename HLCPP>
struct HLCPPToNaturalTraits<std::unique_ptr<HLCPP>> {
  using HLCPPInnerTraits = HLCPPToNaturalTraits<HLCPP>;
  using NaturalType = std::unique_ptr<typename HLCPPInnerTraits::NaturalType>;
  static inline NaturalType Convert(std::unique_ptr<HLCPP>&& hlcpp) {
    if (hlcpp) {
      return std::make_unique<typename HLCPPInnerTraits::NaturalType>(
          HLCPPInnerTraits::Convert(std::move(*hlcpp)));
    }
    return nullptr;
  }
};

/* Natural to HLCPP trait for enums */
template <typename Natural, typename HLCPP>
struct NaturalToHLCPPTraitsEnum {
  using HLCPPType = HLCPP;
  static inline HLCPPType Convert(const Natural& value) {
    return HLCPPType(fidl::ToUnderlying(value));
  }
};

/* HLCPP to Natural trait for enums */
template <typename HLCPP, typename Natural>
struct HLCPPToNaturalTraitsEnum {
  using NaturalType = Natural;
  static inline NaturalType Convert(const HLCPP& value) { return NaturalType(value); }
};

/* Natural to HLCPP trait for bits */
template <typename Natural, typename HLCPP, typename Underlying>
struct NaturalToHLCPPTraitsBits {
  using HLCPPType = HLCPP;
  static inline HLCPPType Convert(Natural&& value) {
    return HLCPPType(static_cast<Underlying>(value));
  }
};

/* HLCPP to Natural trait for bits */
template <typename HLCPP, typename Natural, typename Underlying>
struct HLCPPToNaturalTraitsBits {
  using NaturalType = Natural;
  static inline NaturalType Convert(HLCPP&& value) {
    return NaturalType(static_cast<Underlying>(value));
  }
};

}  // namespace internal

template <typename Natural>
inline auto NaturalToHLCPP(Natural&& value) {
  using Decayed = std::remove_reference_t<std::remove_cv_t<Natural>>;
  using Traits = internal::NaturalToHLCPPTraits<Decayed>;
  return Traits::Convert(std::forward<Decayed>(value));
}

template <typename HLCPP>
inline auto HLCPPToNatural(HLCPP&& value) {
  using Decayed = std::remove_reference_t<std::remove_cv_t<HLCPP>>;
  using Traits = internal::HLCPPToNaturalTraits<Decayed>;
  return Traits::Convert(std::forward<Decayed>(value));
}

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_HLCPP_CONVERSION_H_
