// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_INTERNAL_H_

#include <stddef.h>

#include <ktl/type_traits.h>
#include <sys/types.h>

namespace internal {

// Generates a type whose ::value is true if |T| is allowed to be copied to/from usermode.
//
// The purpose of this type trait is to ensure a stable ABI and prevent bugs by restricting the
// types that may be copied to/from usermode. These types must:
//
//   * Be trivial and can be trivially copied.
//
//   * Have a standard-layout, which ensures their layout won't change from compiler to compiler.
//
//   * Have unique object representations, which ensures they do not contain implicit
//   padding. Copying types with implicit padding can lead information disclosure bugs because the
//   padding may or may not contain uninitialized data.
template <typename T>
struct is_copy_allowed
    : ktl::disjunction<ktl::conjunction<ktl::is_trivial<T>, ktl::is_standard_layout<T>,
                                        ktl::has_unique_object_representations<T>>> {};

// Confine a |vaddr, len| pair to [0, top]; if either vaddr or vaddr+len cross top, fills in {0,0}
void confine_user_address_range(vaddr_t* vaddr, size_t* len, const uintptr_t top);

}  // namespace internal

#endif  // ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_INTERNAL_H_
