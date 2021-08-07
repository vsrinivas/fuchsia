// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

using debug::RegisterID;

using SymbolDataProviderTest = TestWithLoop;

TEST_F(SymbolDataProviderTest, GetRegisters) {
  auto provider = fxl::MakeRefCounted<MockSymbolDataProvider>();

  using RegMap = std::map<RegisterID, std::vector<uint8_t>>;

  // Request no data, the callback should be synchronous.
  bool called = false;
  provider->GetRegisters({}, [&called](const Err& err, RegMap map) {
    called = true;
    EXPECT_TRUE(err.ok());
    EXPECT_TRUE(map.empty());
  });
  EXPECT_TRUE(called);

  // Request one synchronously available register.
  constexpr RegisterID kReg1 = RegisterID::kARMv8_x1;
  std::vector<uint8_t> reg1_value{1, 2, 3, 4, 5, 6, 7, 8};
  provider->AddRegisterValue(kReg1, true, reg1_value);

  called = false;
  provider->GetRegisters({kReg1}, [&](const Err& err, RegMap map) {
    called = true;
    EXPECT_TRUE(err.ok());
    EXPECT_EQ(1u, map.size());
    EXPECT_EQ(reg1_value, map[kReg1]);
  });
  EXPECT_TRUE(called);

  // Request two additional registers that are asynchronously available.
  constexpr RegisterID kReg2 = RegisterID::kARMv8_v2;
  std::vector<uint8_t> reg2_value{9, 8, 7, 6, 5, 4, 3, 2};
  constexpr RegisterID kReg3 = RegisterID::kARMv8_v3;
  std::vector<uint8_t> reg3_value{2, 1, 2, 1, 2, 1, 9, 9};
  provider->AddRegisterValue(kReg2, false, reg2_value);
  provider->AddRegisterValue(kReg3, false, reg3_value);
  called = false;
  provider->GetRegisters({kReg1, kReg2, kReg3}, [&](const Err& err, RegMap map) {
    called = true;
    EXPECT_TRUE(err.ok());
    EXPECT_EQ(3u, map.size());
    EXPECT_EQ(reg1_value, map[kReg1]);
    EXPECT_EQ(reg2_value, map[kReg2]);
    EXPECT_EQ(reg3_value, map[kReg3]);
  });

  // Since it's async, it should not be called until we run the loop.
  EXPECT_FALSE(called);
  loop().RunUntilNoTasks();
  EXPECT_TRUE(called);
}

}  // namespace zxdb
