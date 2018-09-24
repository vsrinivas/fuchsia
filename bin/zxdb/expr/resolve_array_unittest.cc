// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/resolve_array.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/common/test_with_loop.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

class ResolveValueArrayTest : public TestWithLoop {};

}  // namespace

TEST_F(ResolveValueArrayTest, Resolve) {
  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();

  // Request 3 elements from 2-5.
  constexpr uint64_t kBaseAddress = 0x100000;
  constexpr uint32_t kBeginIndex = 2;
  constexpr uint32_t kEndIndex = 5;

  // Array holds uint16_t.
  constexpr uint32_t kTypeSize = 2;
  auto elt_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned,
                                                kTypeSize, "uint16_t");

  // Create memory with two uint16_t's: { 0x1122, 0x3344 }.
  constexpr uint64_t kBeginAddress = kBaseAddress + kBeginIndex * kTypeSize;
  data_provider->AddMemory(kBeginAddress, {0x22, 0x11, 0x44, 0x33});

  bool called = false;
  Err out_err;
  std::vector<ExprValue> out_values;
  ResolveValueArray(data_provider, elt_type.get(), kBaseAddress, kBeginIndex,
                    kEndIndex,
                    [&called, &out_err, &out_values](
                        const Err& err, std::vector<ExprValue> values) {
                      called = true;
                      out_err = err;
                      out_values = std::move(values);
                      debug_ipc::MessageLoop::Current()->QuitNow();
                    });

  // Should be called async.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);

  // The first two elements should be returned, the third one requested will be
  // truncated since the memory was invalid.
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  ASSERT_EQ(2u, out_values.size());

  EXPECT_EQ(elt_type.get(), out_values[0].type());
  EXPECT_EQ(0x1122, out_values[0].GetAs<uint16_t>());
  EXPECT_EQ(kBeginAddress, out_values[0].source().address());

  EXPECT_EQ(elt_type.get(), out_values[1].type());
  EXPECT_EQ(0x3344, out_values[1].GetAs<uint16_t>());
  EXPECT_EQ(kBeginAddress + kTypeSize, out_values[1].source().address());
}

}  // namespace zxdb
