// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/async_dwarf_expr_eval.h"

#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

// This file currently contains some simpler smoketest for the AsyncDwarfExprEval. Most of the
// more complex symbol integration cases are tested by the EvalContextImpl tests which provides most
// of the frontend.

namespace zxdb {

namespace {

class AsyncDwarfExprEvalTest : public TestWithLoop {};

// Provides a way for us to know when the object was deleted.
class TrackedAsyncDwarfExprEval : public AsyncDwarfExprEval {
 public:
  FRIEND_REF_COUNTED_THREAD_SAFE(TrackedAsyncDwarfExprEval);
  FRIEND_MAKE_REF_COUNTED(TrackedAsyncDwarfExprEval);

  // Sets the given boolean in the destructor. The pointer must outlive this class.
  explicit TrackedAsyncDwarfExprEval(EvalCallback cb, fxl::RefPtr<Type> type, bool* destroyed)
      : AsyncDwarfExprEval(std::move(cb), std::move(type)), destroyed_(destroyed) {}

  ~TrackedAsyncDwarfExprEval() override { *destroyed_ = true; }

  bool* destroyed_;
};

}  // namespace

// The memory management of this class is tricky since it keeps itself alive for as long as the
// expression needs evaluating and potentially the pointer value to be fetched. This test validates
// that the object is deleted at the right time.
TEST_F(AsyncDwarfExprEvalTest, MemoryManagement) {
  ProcessSymbolsTestSetup setup;
  setup.InjectMockModule();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);

  // The data provider will vend the memory at the address we'll compute.
  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  constexpr uint8_t kRelativeAddress = 0x99;
  constexpr uint64_t kAbsoluteAddress =
      ProcessSymbolsTestSetup::kDefaultLoadAddress + kRelativeAddress;

  // The thing the expression points to.
  auto uint32_type = MakeInt32Type();
  constexpr uint32_t kMemoryValue = 0x12345678;
  data_provider->AddMemory(kAbsoluteAddress, {0x78, 0x56, 0x34, 0x12});

  auto eval_context =
      fxl::MakeRefCounted<EvalContextImpl>(setup.process().GetWeakPtr(), data_provider);

  // This expression evaluates the relative address.
  std::vector<uint8_t> expr{llvm::dwarf::DW_OP_addr, kRelativeAddress, 0, 0, 0, 0, 0, 0, 0};

  bool called = false;
  auto value_callback = [&called, kMemoryValue](ErrOrValue value) {
    called = true;
    EXPECT_TRUE(value.ok());
    EXPECT_EQ(kMemoryValue, value.value().GetAs<uint32_t>());
  };

  bool destroyed = false;
  auto eval =
      fxl::MakeRefCounted<TrackedAsyncDwarfExprEval>(value_callback, uint32_type, &destroyed);
  eval->Eval(eval_context, symbol_context, expr);

  // Should evaluate asynchronously.
  EXPECT_FALSE(called);

  // Now that evaluation has started, we can release our reference and it should not be destroyed.
  eval = nullptr;
  ASSERT_FALSE(destroyed);

  loop().RunUntilNoTasks();

  // Async running should allow the result to be computed and the evaluator to be destroyed.
  EXPECT_TRUE(called);
  EXPECT_TRUE(destroyed);
}

}  // namespace zxdb
