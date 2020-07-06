// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Value builders for use in benchmarks.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_BUILDER_H_
#define SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_BUILDER_H_

#include <benchmarkfidl/llcpp/fidl.h>

namespace benchmark_suite {

llcpp::benchmarkfidl::Table1Struct BuildTable_AllSet_1();
llcpp::benchmarkfidl::Table16Struct BuildTable_AllSet_16();
llcpp::benchmarkfidl::Table256Struct BuildTable_AllSet_256();
llcpp::benchmarkfidl::Table1Struct BuildTable_Unset_1();
llcpp::benchmarkfidl::Table16Struct BuildTable_Unset_16();
llcpp::benchmarkfidl::Table256Struct BuildTable_Unset_256();
llcpp::benchmarkfidl::Table1Struct BuildTable_SingleSet_1_of_1();
llcpp::benchmarkfidl::Table16Struct BuildTable_SingleSet_1_of_16();
llcpp::benchmarkfidl::Table16Struct BuildTable_SingleSet_16_of_16();
llcpp::benchmarkfidl::Table256Struct BuildTable_SingleSet_1_of_256();
llcpp::benchmarkfidl::Table256Struct BuildTable_SingleSet_16_of_256();
llcpp::benchmarkfidl::Table256Struct BuildTable_SingleSet_256_of_256();

}  // namespace benchmark_suite

#endif  // SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_BUILDER_H_
