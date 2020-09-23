// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_TYPE_TRAITS_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_TYPE_TRAITS_H_

#include <type_traits>

namespace ktl {

using std::is_array;
using std::is_array_v;
using std::is_bounded_array;
using std::is_bounded_array_v;
using std::is_unbounded_array;
using std::is_unbounded_array_v;

using std::conjunction;
using std::conjunction_v;

using std::disjunction;
using std::disjunction_v;

using std::conditional;

using std::decay;
using std::decay_t;

using std::enable_if;
using std::enable_if_t;

using std::has_unique_object_representations;
using std::has_unique_object_representations_v;

using std::is_const;
using std::is_const_v;

using std::is_copy_constructible;
using std::is_copy_constructible_v;

using std::is_copy_assignable;
using std::is_copy_assignable_v;

using std::is_lvalue_reference;
using std::is_lvalue_reference_v;

using std::is_move_constructible;
using std::is_move_constructible_v;

using std::is_move_assignable;
using std::is_move_assignable_v;

using std::is_pod;
using std::is_pod_v;

using std::is_same;
using std::is_same_v;

using std::is_standard_layout;
using std::is_standard_layout_v;

using std::is_trivial;
using std::is_trivial_v;

using std::is_trivially_copy_assignable;
using std::is_trivially_copy_assignable_v;

using std::is_trivially_copy_constructible;
using std::is_trivially_copy_constructible_v;

using std::is_trivially_destructible;
using std::is_trivially_destructible_v;

using std::is_trivially_move_assignable;
using std::is_trivially_move_assignable_v;

using std::is_trivially_move_constructible;
using std::is_trivially_move_constructible_v;

using std::is_void;
using std::is_void_v;

using std::remove_all_extents;
using std::remove_all_extents_t;

using std::remove_const;
using std::remove_const_t;

using std::remove_extent;
using std::remove_extent_t;

using std::remove_pointer;
using std::remove_pointer_t;

using std::remove_reference;
using std::remove_reference_t;

// Generates a type whose ::value is true if |T| is the same type as one of the types in the |Ts|
// parameter pack.
//
// For example:
//
// ktl::is_one_of<T, bool, char, int>::value will be true if and only if T is a bool, char, or int.
//
template <typename T, typename... Ts>
struct is_one_of : ktl::disjunction<ktl::is_same<T, Ts>...> {};

template <class T, typename... Ts>
inline constexpr bool is_one_of_v = ktl::is_one_of<T, Ts...>::value;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_TYPE_TRAITS_H_
