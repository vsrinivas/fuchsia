// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Value builders for use in benchmarks.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_BUILDER_H_
#define SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_BUILDER_H_

#include <benchmarkfidl/llcpp/fidl.h>

namespace benchmark_suite {

llcpp::benchmarkfidl::ByteVector Build_ByteVector_16();
llcpp::benchmarkfidl::ByteVector Build_ByteVector_256();
llcpp::benchmarkfidl::ByteVector Build_ByteVector_4096();
llcpp::benchmarkfidl::Table1Struct Build_Table_AllSet_1();
llcpp::benchmarkfidl::Table16Struct Build_Table_AllSet_16();
llcpp::benchmarkfidl::Table256Struct Build_Table_AllSet_256();
llcpp::benchmarkfidl::Table1Struct Build_Table_Unset_1();
llcpp::benchmarkfidl::Table16Struct Build_Table_Unset_16();
llcpp::benchmarkfidl::Table256Struct Build_Table_Unset_256();
llcpp::benchmarkfidl::Table1Struct Build_Table_SingleSet_1_of_1();
llcpp::benchmarkfidl::Table16Struct Build_Table_SingleSet_1_of_16();
llcpp::benchmarkfidl::Table16Struct Build_Table_SingleSet_16_of_16();
llcpp::benchmarkfidl::Table256Struct Build_Table_SingleSet_1_of_256();
llcpp::benchmarkfidl::Table256Struct Build_Table_SingleSet_16_of_256();
llcpp::benchmarkfidl::Table256Struct Build_Table_SingleSet_256_of_256();
llcpp::benchmarkfidl::PaddedStructTree8 Build_PaddedStructTree_Depth8();
llcpp::benchmarkfidl::StructTree8 Build_StructTree_Depth8();

}  // namespace benchmark_suite

#endif  // SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_BUILDER_H_
