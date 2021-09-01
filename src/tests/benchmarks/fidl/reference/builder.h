// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Value builders for use in benchmarks.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_BUILDER_H_
#define SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_BUILDER_H_

#include <fidl/benchmarkfidl/cpp/wire.h>

namespace benchmark_suite {

benchmarkfidl::wire::ByteVector Build_ByteVector_16(fidl::AnyArena& allocator);
benchmarkfidl::wire::ByteVector Build_ByteVector_256(fidl::AnyArena& allocator);
benchmarkfidl::wire::ByteVector Build_ByteVector_4096(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table1Struct Build_Table_AllSet_1(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table16Struct Build_Table_AllSet_16(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table256Struct Build_Table_AllSet_256(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table1Struct Build_Table_Unset_1(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table16Struct Build_Table_Unset_16(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table256Struct Build_Table_Unset_256(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table1Struct Build_Table_SingleSet_1_of_1(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table16Struct Build_Table_SingleSet_1_of_16(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table16Struct Build_Table_SingleSet_16_of_16(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table256Struct Build_Table_SingleSet_1_of_256(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table256Struct Build_Table_SingleSet_16_of_256(fidl::AnyArena& allocator);
benchmarkfidl::wire::Table256Struct Build_Table_SingleSet_256_of_256(fidl::AnyArena& allocator);
benchmarkfidl::wire::PaddedStructTree8 Build_PaddedStructTree_Depth8(fidl::AnyArena& allocator);
benchmarkfidl::wire::StructTree8 Build_StructTree_Depth8(fidl::AnyArena& allocator);

}  // namespace benchmark_suite

#endif  // SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_BUILDER_H_
