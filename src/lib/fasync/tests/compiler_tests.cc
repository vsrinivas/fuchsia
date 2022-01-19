// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/internal/compiler.h>

#include <zxtest/zxtest.h>

namespace {

static_assert(LIB_FASYNC_CPP_STANDARD_SUPPORTED(14), "");
static_assert(LIB_FASYNC_CPP14_SUPPORTED, "");

static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(noreturn), "");
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(deprecated), "");

#if __cplusplus < 201703L

static_assert(!LIB_FASYNC_HAS_CPP_FEATURE(deduction_guides), "");
static_assert(!LIB_FASYNC_HAS_CPP_FEATURE(inline_variables), "");
static_assert(!LIB_FASYNC_HAS_CPP_FEATURE(consteval), "");
static_assert(!LIB_FASYNC_HAS_CPP_FEATURE(concepts), "");

static_assert(!LIB_FASYNC_HAS_CPP_ATTRIBUTE(fallthrough), "");
static_assert(!LIB_FASYNC_HAS_CPP_ATTRIBUTE(nodiscard), "");

#endif

#if __cplusplus >= 201703L && __cplusplus < 202002L

static_assert(LIB_FASYNC_CPP_STANDARD_SUPPORTED(17));
static_assert(LIB_FASYNC_CPP17_SUPPORTED);

static_assert(LIB_FASYNC_HAS_CPP_FEATURE(deduction_guides));
static_assert(LIB_FASYNC_HAS_CPP_FEATURE(inline_variables));
static_assert(!LIB_FASYNC_HAS_CPP_FEATURE(consteval));
static_assert(!LIB_FASYNC_HAS_CPP_FEATURE(concepts));

static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(fallthrough));
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(nodiscard));
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(maybe_unused));

// Clang seems to backport these
#ifdef __clang__
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(likely));
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(unlikely));
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(no_unique_address));
#endif

#endif

#if __cplusplus > 201703L

static_assert(LIB_FASYNC_CPP_STANDARD_SUPPORTED(20));
static_assert(LIB_FASYNC_CPP20_SUPPORTED);

static_assert(LIB_FASYNC_HAS_CPP_FEATURE(deduction_guides));
static_assert(LIB_FASYNC_HAS_CPP_FEATURE(inline_variables));
static_assert(LIB_FASYNC_HAS_CPP_FEATURE(consteval));
static_assert(LIB_FASYNC_HAS_CPP_FEATURE(concepts));

static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(fallthrough));
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(nodiscard));
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(maybe_unused));
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(likely));
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(unlikely));
static_assert(LIB_FASYNC_HAS_CPP_ATTRIBUTE(no_unique_address));

#endif

}  // namespace
