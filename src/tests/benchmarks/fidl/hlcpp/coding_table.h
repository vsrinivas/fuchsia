// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_CODING_TABLE_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_CODING_TABLE_H_

#include <lib/fidl/cpp/coding_traits.h>
#include <lib/fidl/internal.h>

namespace hlcpp_benchmarks {

template <typename FidlType>
constexpr size_t EncodedSize = fidl::CodingTraits<FidlType>::inline_size_v2;

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_CODING_TABLE_H_
