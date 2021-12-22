// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_through_plt_thread_controller.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"

namespace zxdb {

// IMPORTANT NOTE: The basics of the PLT thread controller are covered by the
// function_thread_controller_unittest.cc tests.

namespace {

using StepThroughPltThreadControllerTest = InlineThreadControllerTest;

}  // namespace

// Tests that the thread controller stops if a destination of the PLT jump can not be found.
TEST_F(StepThroughPltThreadControllerTest, NoDest) {
  SymbolContext sym_context(kSymbolizedModuleAddress);

  // PLT symbol info. This thunk is in the symbolized module to call into the unsymbolized one.
  const uint64_t kAddrPltRelative = 0x5980;
  const uint64_t kAddrPltAbsolute = kAddrPltRelative + kSymbolizedModuleAddress;
  const std::string kPltName = "plt_call";
  ElfSymbolRecord plt_record(ElfSymbolType::kPlt, kAddrPltRelative, 1, kPltName);
  auto plt_symbol = fxl::MakeRefCounted<ElfSymbol>(module_symbols()->GetWeakPtr(), plt_record);

  Location plt_loc(kAddrPltAbsolute, FileLine(), 0, sym_context, plt_symbol);
  Identifier plt_identifier(IdentifierComponent(SpecialIdentifier::kPlt, kPltName));
  module_symbols()->AddSymbolLocations(plt_identifier, {plt_loc});

  // Set an initial stop at the PLT location.
  constexpr uint64_t kSrcSP = 0x5000;
  std::vector<std::unique_ptr<Frame>> stack;
  stack.push_back(std::make_unique<MockFrame>(&session(), thread(), plt_loc, kSrcSP, kSrcSP));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep, std::move(stack), true);

  // Do a PLT step.
  auto step_into = std::make_unique<StepThroughPltThreadController>();
  bool callback_issued = false;
  thread()->ContinueWith(std::move(step_into), [&callback_issued](const Err& err) {
    callback_issued = true;

    // This should fail with the PLT destination error message.
    EXPECT_TRUE(err.has_error());
    EXPECT_EQ(err.msg(), "Could not find destination of PLT trampoline.");
  });
  EXPECT_TRUE(callback_issued);

  // Should not have resumed.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());
}

}  // namespace zxdb
