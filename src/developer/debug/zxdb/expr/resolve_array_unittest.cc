// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_array.h"
#include "garnet/bin/zxdb/symbols/array_type.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

namespace {

class ResolveArrayTest : public TestWithLoop {};

}  // namespace

TEST_F(ResolveArrayTest, ResolveStatic) {
  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();

  // Request 3 elements from 1-4.
  constexpr uint64_t kBaseAddress = 0x100000;
  constexpr uint32_t kBeginIndex = 1;
  constexpr uint32_t kEndIndex = 4;

  // Array holds 3 uint16_t.
  constexpr uint32_t kTypeSize = 2;
  auto elt_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned,
                                                kTypeSize, "uint16_t");
  auto array_type = fxl::MakeRefCounted<ArrayType>(elt_type, 3);

  // Values are 0x1122, 0x3344, 0x5566
  std::vector<uint8_t> array_bytes = {0x22, 0x11, 0x44, 0x33, 0x66, 0x55};
  ExprValue value(array_type, array_bytes, ExprValueSource(kBaseAddress));

  std::vector<ExprValue> result;
  Err err = ResolveArray(value, kBeginIndex, kEndIndex, &result);
  EXPECT_FALSE(err.has_error());

  // Should have returned two values (the overlap of the array and the
  // requested range).
  ASSERT_EQ(2u, result.size());

  EXPECT_EQ(elt_type.get(), result[0].type());
  EXPECT_EQ(0x3344, result[0].GetAs<uint16_t>());
  EXPECT_EQ(kBaseAddress + kTypeSize, result[0].source().address());

  EXPECT_EQ(elt_type.get(), result[1].type());
  EXPECT_EQ(0x5566, result[1].GetAs<uint16_t>());
  EXPECT_EQ(kBaseAddress + kTypeSize * 2, result[1].source().address());
}

// Resolves an array element with a pointer as the base.
TEST_F(ResolveArrayTest, ResolvePointer) {
  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();

  // Request 3 elements from 1-4.
  constexpr uint64_t kBaseAddress = 0x100000;
  constexpr uint32_t kBeginIndex = 1;
  constexpr uint32_t kEndIndex = 4;

  // Array holds 3 uint16_t.
  constexpr uint32_t kTypeSize = 2;
  auto elt_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned,
                                                kTypeSize, "uint16_t");
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                    LazySymbol(elt_type));

  // Create memory with two values 0x3344, 0x5566. Note that these are offset
  // one value from the beginning of the array so the requested address of the
  // kBeginIndex'th element matches this address.
  constexpr uint64_t kBeginAddress = kBaseAddress + kBeginIndex * kTypeSize;
  data_provider->AddMemory(kBeginAddress, {0x44, 0x33, 0x66, 0x55});

  // Data in the value is the pointer to the beginning of the array.
  ExprValue value(ptr_type, {0, 0, 0x10, 0, 0, 0, 0, 0});

  bool called = false;
  Err out_err;
  std::vector<ExprValue> result;
  ResolveArray(data_provider, value, kBeginIndex, kEndIndex,
               [&called, &out_err, &result](const Err& err,
                                            std::vector<ExprValue> values) {
                 called = true;
                 out_err = err;
                 result = std::move(values);
                 debug_ipc::MessageLoop::Current()->QuitNow();
               });

  // Should be called async.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);

  // Should have returned two values (the overlap of the array and the
  // requested range).
  ASSERT_EQ(2u, result.size());

  EXPECT_EQ(elt_type.get(), result[0].type());
  EXPECT_EQ(0x3344, result[0].GetAs<uint16_t>());
  EXPECT_EQ(kBaseAddress + kTypeSize, result[0].source().address());

  EXPECT_EQ(elt_type.get(), result[1].type());
  EXPECT_EQ(0x5566, result[1].GetAs<uint16_t>());
  EXPECT_EQ(kBaseAddress + kTypeSize * 2, result[1].source().address());
}

}  // namespace zxdb
