// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_TYPE_TRAITS_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_TYPE_TRAITS_H_

#include <lib/stdcompat/type_traits.h>

namespace ktl {

using cpp20::is_bounded_array;
using cpp20::is_bounded_array_v;
using cpp20::is_unbounded_array;
using cpp20::is_unbounded_array_v;
using std::is_array;
using std::is_array_v;

using std::conjunction;
using std::conjunction_v;

using std::disjunction;
using std::disjunction_v;

using std::conditional;
using std::conditional_t;

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

using std::is_default_constructible;
using std::is_default_constructible_v;

using std::is_enum;
using std::is_enum_v;

using std::is_floating_point;
using std::is_floating_point_v;

using std::is_function;
using std::is_function_v;

using std::is_integral;
using std::is_integral_v;

using std::invoke_result_t;
using std::is_invocable;
using std::is_invocable_r;
using std::is_invocable_r_v;
using std::is_invocable_v;

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

using std::is_signed;
using std::is_signed_v;

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

using std::aligned_storage;
using std::aligned_storage_t;

using std::underlying_type;
using std::underlying_type_t;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_TYPE_TRAITS_H_
