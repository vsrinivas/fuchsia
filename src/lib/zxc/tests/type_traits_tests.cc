// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define ZXC_TYPE_TRAITS_INTERNAL_TEST
#include <lib/fitx/internal/type_traits.h>

static_assert(std::is_same<::fitx::internal::void_t<>, void>::value);
static_assert(std::is_same<::fitx::internal::void_t<int>, void>::value);
static_assert(std::is_same<::fitx::internal::void_t<int, int>, void>::value);

static_assert(::fitx::internal::conjunction_v<> == true);
static_assert(::fitx::internal::conjunction_v<std::false_type> == false);
static_assert(::fitx::internal::conjunction_v<std::true_type> == true);
static_assert(::fitx::internal::conjunction_v<std::false_type, std::false_type> == false);
static_assert(::fitx::internal::conjunction_v<std::false_type, std::true_type> == false);
static_assert(::fitx::internal::conjunction_v<std::true_type, std::false_type> == false);
static_assert(::fitx::internal::conjunction_v<std::true_type, std::true_type> == true);

static_assert(::fitx::internal::conjunction_v<std::false_type, std::false_type, std::false_type,
                                            std::false_type, std::false_type, std::false_type> ==
              false);
static_assert(::fitx::internal::conjunction_v<std::false_type, std::false_type, std::false_type,
                                            std::false_type, std::false_type, std::true_type> ==
              false);
static_assert(::fitx::internal::conjunction_v<std::true_type, std::true_type, std::true_type,
                                            std::true_type, std::true_type, std::true_type> ==
              true);
static_assert(::fitx::internal::conjunction_v<std::true_type, std::true_type, std::true_type,
                                            std::true_type, std::true_type, std::false_type> ==
              false);

static_assert(::fitx::internal::disjunction_v<> == false);
static_assert(::fitx::internal::disjunction_v<std::false_type> == false);
static_assert(::fitx::internal::disjunction_v<std::true_type> == true);
static_assert(::fitx::internal::disjunction_v<std::false_type, std::false_type> == false);
static_assert(::fitx::internal::disjunction_v<std::false_type, std::true_type> == true);
static_assert(::fitx::internal::disjunction_v<std::true_type, std::false_type> == true);
static_assert(::fitx::internal::disjunction_v<std::true_type, std::true_type> == true);

static_assert(::fitx::internal::disjunction_v<std::false_type, std::false_type, std::false_type,
                                            std::false_type, std::false_type, std::false_type> ==
              false);
static_assert(::fitx::internal::disjunction_v<std::false_type, std::false_type, std::false_type,
                                            std::false_type, std::false_type, std::true_type> ==
              true);
static_assert(::fitx::internal::disjunction_v<std::true_type, std::true_type, std::true_type,
                                            std::true_type, std::true_type, std::true_type> ==
              true);
static_assert(::fitx::internal::disjunction_v<std::true_type, std::true_type, std::true_type,
                                            std::true_type, std::true_type, std::false_type> ==
              true);

static_assert(::fitx::internal::negation_v<std::false_type> == true);
static_assert(::fitx::internal::negation_v<std::true_type> == false);
