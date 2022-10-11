// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_VARIANT_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_VARIANT_H_

#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <variant>

#include "../array.h"
#include "../functional.h"
#include "../type_traits.h"
#include "exception.h"

namespace cpp17 {

namespace internal {

template <typename T, typename... Ts>
using are_equal = cpp17::conjunction<std::is_same<T, Ts>...>;

template <typename... T>
constexpr bool are_equal_v =
    sizeof...(T) < 2 ? true : are_equal<std::tuple_element_t<0, std::tuple<T...>>, T...>::value;

// Helper type to avoid recursive instantiations of the full variant type.
template <typename...>
struct variant_list {};

// Gets the number of alternatives in a variant_list as a compile-time constant.
template <typename T>
struct variant_list_size;

template <typename... Ts>
struct variant_list_size<variant_list<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {
};

// Helper to get the type of a variant_list alternative with the given index.
template <std::size_t Index, typename VariantList>
struct variant_alternative;

template <std::size_t Index, typename T0, typename... Ts>
struct variant_alternative<Index, variant_list<T0, Ts...>>
    : variant_alternative<Index - 1, variant_list<Ts...>> {};

template <typename T0, typename... Ts>
struct variant_alternative<0, variant_list<T0, Ts...>> {
  using type = T0;
};

// The purpose is to provide index flattening such that at compile and runtime
// set of active indexes can be mapped into a flat array.
struct visit_index {
  template <typename T>
  static constexpr const T& at(const T& element) {
    return element;
  }

  template <typename T, std::size_t N, typename... Indexes>
  static constexpr auto&& at(const std::array<T, N>& matrix, std::size_t index, Indexes... is) {
    return at(matrix[index], is...);
  }
};

template <typename Visitor, typename... Variants>
struct visit_matrix : public visit_index {
  template <typename... Dispatchers>
  static constexpr auto make_dispatchers_array(Dispatchers... dispatchers) {
    using result_type = std::array<std::common_type_t<cpp20::remove_cvref_t<Dispatchers>...>,
                                   sizeof...(Dispatchers)>;
    return result_type{dispatchers...};
  }

  // Creates a flat array, whose entry corresponds to a functor doing the
  // dispatch.
  template <typename DispatcherContainer, std::size_t... Active>
  static constexpr auto make_dispatcher_array(std::index_sequence<Active...> active) {
    return DispatcherContainer::template dispatcher<Active...>::template dispatch<Visitor,
                                                                                  Variants...>;
  }

  // Active is the set of active variant index to be converted into a
  // dispatcher, Current is the set of possible valid active indexes for the
  // current variant. Remaining is the set of index sequences for each variant.
  // Essentially this is creating a nested |sizeof...(Variants)|-dimensional
  // array,
  template <typename DipatcherContainer, std::size_t... Active, std::size_t... Current,
            typename... Remaining>
  static constexpr auto make_dispatcher_array(std::index_sequence<Active...> active,
                                              std::index_sequence<Current...> current,
                                              Remaining... remaining) {
    return make_dispatchers_array(make_dispatcher_array<DipatcherContainer>(
        std::index_sequence<Active..., Current>{}, remaining...)...);
  }

  template <typename DispatcherContainer, typename... Args>
  static constexpr auto make_dispatcher_array() {
    return make_dispatcher_array<DispatcherContainer>(std::index_sequence<>{}, Args{}...);
  }
};

}  // namespace internal

}  // namespace cpp17

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_VARIANT_H_
