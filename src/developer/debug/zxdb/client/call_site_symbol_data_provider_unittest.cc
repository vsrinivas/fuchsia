// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/call_site_symbol_data_provider.h"

#include <gtest/gtest.h>

#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/zxdb/client/mock_process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/symbols/call_site.h"
#include "src/developer/debug/zxdb/symbols/call_site_parameter.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

using debug_ipc::RegisterID;

using CallSiteSymbolDataProviderTest = TestWithLoop;

TEST_F(CallSiteSymbolDataProviderTest, GetRegisterAsync) {
  constexpr uint64_t kModuleLoad = 0x1000000;
  SymbolContext symbol_context(kModuleLoad);
  constexpr TargetPointer kRelativeReturnPC = 0x1000;
  // constexpr TargetPointer kAbsoluteReturnPC = kModuleLoad + kRelativeReturnPC;

  // The statically-known register and the provider for it (this corresponds to the calling frame
  // without the CallSite information).
  constexpr RegisterID kStaticRegId = RegisterID::kARMv8_x6;
  constexpr uint64_t kStaticRegValue = 0x4000;
  auto frame_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  frame_provider->AddRegisterValue(kStaticRegId, true, kStaticRegValue);

  // DWARF register number of the register provided by the entry value and how to compute it. It
  // is expressed in terms of "register 6" which we provide above.
  constexpr uint32_t kEntryRegDwarfNum = 1;
  constexpr RegisterID kEntryRegId = RegisterID::kARMv8_x1;
  constexpr uint8_t kEntryRegOffset = 0x10;  // Add this to kStaticRegValue to get the result.
  constexpr uint64_t kEntryRegValue = kStaticRegValue + kEntryRegOffset;
  std::vector<uint8_t> entry_expression{
      llvm::dwarf::DW_OP_breg6,
      kEntryRegOffset,
  };

  auto param =
      fxl::MakeRefCounted<CallSiteParameter>(kEntryRegDwarfNum, DwarfExpr(entry_expression));
  auto call_site = fxl::MakeRefCounted<CallSite>(kRelativeReturnPC, std::vector<LazySymbol>{param});

  Session session;
  MockProcess process(&session);

  auto call_site_provider = fxl::MakeRefCounted<CallSiteSymbolDataProvider>(
      process.GetWeakPtr(), call_site, symbol_context, frame_provider);

  // The statically known register should be synchronously provided by the call site provider.
  auto opt_view = call_site_provider->GetRegister(kStaticRegId);
  ASSERT_TRUE(opt_view);
  EXPECT_EQ(sizeof(kStaticRegValue), opt_view->size());

  // Synchronous calls for the CallSite-provided register will fail since they require an
  // expression evaluation. This values is known to be unknown, so returns a valid but empty array.
  opt_view = call_site_provider->GetRegister(kEntryRegId);
  ASSERT_TRUE(opt_view);
  EXPECT_TRUE(opt_view->empty());

  // Request asynchronously, this should succeed asynchronously.
  std::optional<std::vector<uint8_t>> result_data;
  call_site_provider->GetRegisterAsync(kEntryRegId,
                                       [&result_data](const Err& err, std::vector<uint8_t> data) {
                                         EXPECT_TRUE(err.ok()) << err.msg();
                                         result_data = std::move(data);
                                       });
  EXPECT_FALSE(result_data);  // Shouldn't succeed synchronsly.

  // Should succeed asynchronously.
  loop().RunUntilNoTasks();
  ASSERT_TRUE(result_data);

  // Currently the result of the expression is the size of a StackEntry which is normally different
  // than the requested register. This is good enough for our use-case. We validate that to make
  // sure it's not some truncated value. The code may change to match the requested register size,
  // in which case the expected length should be 8.
  EXPECT_EQ(sizeof(DwarfExprEval::StackEntry), result_data->size());
  result_data->resize(sizeof(kEntryRegValue));  // Trim high 0's.

  // Validate the result.
  std::vector<uint8_t> expected(sizeof(kEntryRegValue));
  memcpy(expected.data(), &kEntryRegValue, sizeof(kEntryRegValue));
  EXPECT_EQ(expected, *result_data);
}

}  // namespace zxdb
