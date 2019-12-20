// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"

#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

namespace {

using debug_ipc::RegisterID;

// Base address of the imaginary module. Relative addresses will be relative to
// this number.
constexpr TargetPointer kModuleBase = 0x78000000;

class DwarfExprEvalTest : public TestWithLoop {
 public:
  DwarfExprEvalTest() : provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) {}

  DwarfExprEval& eval() { return eval_; }
  fxl::RefPtr<MockSymbolDataProvider> provider() { return provider_; }
  const SymbolContext symbol_context() const { return symbol_context_; }

  // If expected_message is non-null, this error message will be expected on failure. The expected
  // result will only be checked on success, true, and the expected_message will only be checked on
  // failure.
  //
  // If the expected_result_type is "kData", the expected_result will be ignored. The caller should
  // manually validate the result.
  //
  // The DwarfExprEval used in the computation will be in the completed state so tests can check
  // eval().whatever for additional validation after this call returns.
  void DoEvalTest(const std::vector<uint8_t> data, bool expected_success,
                  DwarfExprEval::Completion expected_completion, uint128_t expected_result,
                  DwarfExprEval::ResultType expected_result_type,
                  const char* expected_message = nullptr);

 private:
  DwarfExprEval eval_;
  fxl::RefPtr<MockSymbolDataProvider> provider_;
  SymbolContext symbol_context_ = SymbolContext(kModuleBase);
};

void DwarfExprEvalTest::DoEvalTest(const std::vector<uint8_t> data, bool expected_success,
                                   DwarfExprEval::Completion expected_completion,
                                   uint128_t expected_result,
                                   DwarfExprEval::ResultType expected_result_type,
                                   const char* expected_message) {
  eval_.Clear();

  bool callback_issued = false;
  EXPECT_EQ(
      expected_completion,
      eval_.Eval(provider(), symbol_context_, data,
                 [&callback_issued, expected_success, expected_completion, expected_result,
                  expected_result_type, expected_message](DwarfExprEval* eval, const Err& err) {
                   EXPECT_TRUE(eval->is_complete());
                   EXPECT_EQ(expected_success, !err.has_error()) << err.msg();
                   if (err.ok()) {
                     EXPECT_EQ(expected_result_type, eval->GetResultType());
                     if (expected_result_type != DwarfExprEval::ResultType::kData)
                       EXPECT_EQ(expected_result, eval->GetResult());
                   } else if (expected_message) {
                     EXPECT_EQ(expected_message, err.msg());
                   }
                   callback_issued = true;

                   // When we're doing an async completion, need to exit the
                   // message loop to continue with the test.
                   if (expected_completion == DwarfExprEval::Completion::kAsync)
                     debug_ipc::MessageLoop::Current()->QuitNow();
                 }));

  if (expected_completion == DwarfExprEval::Completion::kAsync) {
    // In the async case the message loop needs to be run to get the result.
    EXPECT_FALSE(eval_.is_complete());
    EXPECT_FALSE(callback_issued);

    // Ensure the callback was made after running the loop.
    loop().Run();
  }

  EXPECT_TRUE(eval_.is_complete());
  EXPECT_TRUE(callback_issued);
}

const debug_ipc::RegisterID kDWARFReg0ID = debug_ipc::RegisterID::kARMv8_x0;
const debug_ipc::RegisterID kDWARFReg1ID = debug_ipc::RegisterID::kARMv8_x1;
const debug_ipc::RegisterID kDWARFReg3ID = debug_ipc::RegisterID::kARMv8_x3;
const debug_ipc::RegisterID kDWARFReg4ID = debug_ipc::RegisterID::kARMv8_x4;
const debug_ipc::RegisterID kDWARFReg6ID = debug_ipc::RegisterID::kARMv8_x6;
const debug_ipc::RegisterID kDWARFReg9ID = debug_ipc::RegisterID::kARMv8_x9;

}  // namespace

TEST_F(DwarfExprEvalTest, NoResult) {
  const char kNoResults[] = "DWARF expression produced no results.";

  // Empty expression.
  DoEvalTest({}, false, DwarfExprEval::Completion::kSync, 0, DwarfExprEval::ResultType::kPointer,
             kNoResults);
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_TRUE(eval().result_is_constant());

  // Nonempty expression that produces no results.
  DoEvalTest({llvm::dwarf::DW_OP_nop}, false, DwarfExprEval::Completion::kSync, 0,
             DwarfExprEval::ResultType::kPointer, kNoResults);
}

TEST_F(DwarfExprEvalTest, MarkValue) {
  // A computation without "stack_value" should report the result type as a pointers.
  DoEvalTest({llvm::dwarf::DW_OP_lit4}, true, DwarfExprEval::Completion::kSync, 4u,
             DwarfExprEval::ResultType::kPointer);
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_TRUE(eval().result_is_constant());

  // "stack value" should mark the result as a stack value.
  DoEvalTest({llvm::dwarf::DW_OP_lit4, llvm::dwarf::DW_OP_stack_value}, true,
             DwarfExprEval::Completion::kSync, 4u, DwarfExprEval::ResultType::kValue);
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_TRUE(eval().result_is_constant());
}

// Tests that we can recover from infinite loops and destroy the evaluator
// when it's got an asynchronous operation pending.
TEST_F(DwarfExprEvalTest, InfiniteLoop) {
  // This expression loops back to the beginning infinitely.
  std::vector<uint8_t> loop_data = {llvm::dwarf::DW_OP_skip, 0xfd, 0xff};

  std::unique_ptr<DwarfExprEval> eval = std::make_unique<DwarfExprEval>();

  bool callback_issued = false;
  eval->Eval(provider(), symbol_context(), loop_data,
             [&callback_issued](DwarfExprEval* eval, const Err& err) { callback_issued = true; });

  // Let the message loop process messages for a few times so the evaluator can
  // run.
  loop().PostTask(FROM_HERE, []() { debug_ipc::MessageLoop::Current()->QuitNow(); });
  loop().Run();
  loop().PostTask(FROM_HERE, []() { debug_ipc::MessageLoop::Current()->QuitNow(); });
  loop().Run();

  // Reset the evaluator, this should cancel everything.
  eval.reset();

  // This should not crash (the evaluator may have posted a pending task
  // that will get executed when we run the loop again, and it should notice
  // the object is gone).
  loop().PostTask(FROM_HERE, []() { debug_ipc::MessageLoop::Current()->QuitNow(); });
  loop().Run();

  // Callback should never have been issued.
  EXPECT_FALSE(callback_issued);
}

// Tests synchronously reading a single register.
TEST_F(DwarfExprEvalTest, SyncRegister) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);

  DoEvalTest({llvm::dwarf::DW_OP_reg0}, true, DwarfExprEval::Completion::kSync, kValue,
             DwarfExprEval::ResultType::kValue);
  EXPECT_EQ(RegisterID::kARMv8_x0, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

// Tests the encoding form of registers as parameters to an operation rather
// than the version encoded in the operation.
//
// Also tests DW_OP_nop.
TEST_F(DwarfExprEvalTest, SyncRegisterAsNumber) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg1ID, true, kValue);

  // Use "regx" which will read the register number as a ULEB following it.
  // The byte is the ULEB-encoded version of 1 (high bit set to indicate it's
  // the last byte).
  std::vector<uint8_t> expr_data;
  expr_data.push_back(llvm::dwarf::DW_OP_nop);
  expr_data.push_back(llvm::dwarf::DW_OP_regx);
  expr_data.push_back(0b10000001);

  DoEvalTest(expr_data, true, DwarfExprEval::Completion::kSync, kValue,
             DwarfExprEval::ResultType::kValue);
  EXPECT_EQ(RegisterID::kARMv8_x1, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

// Tests asynchronously reading a single register.
TEST_F(DwarfExprEvalTest, AsyncRegister) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);

  DoEvalTest({llvm::dwarf::DW_OP_reg0}, true, DwarfExprEval::Completion::kAsync, kValue,
             DwarfExprEval::ResultType::kValue);
  EXPECT_EQ(RegisterID::kARMv8_x0, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

// Tests synchronously hitting an invalid opcode.
TEST_F(DwarfExprEvalTest, SyncInvalidOp) {
  // Make a program that consists only of a user-defined opcode (not supported).
  // Can't use DW_OP_lo_user because that's a GNU TLS extension we know about.
  DoEvalTest({llvm::dwarf::DW_OP_lo_user + 1}, false, DwarfExprEval::Completion::kSync, 0,
             DwarfExprEval::ResultType::kValue, "Invalid opcode 0xe1 in DWARF expression.");
}

// Tests synchronously hitting an invalid opcode (async error handling).
TEST_F(DwarfExprEvalTest, AsyncInvalidOp) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);

  // Make a program that consists of getting an async register and then
  // executing an invalid opcode. Can't use DW_OP_lo_user because that's a GNU
  // TLS extension we know about.
  std::vector<uint8_t> expr_data;
  expr_data.push_back(llvm::dwarf::DW_OP_reg0);
  expr_data.push_back(llvm::dwarf::DW_OP_lo_user + 1);

  DoEvalTest(expr_data, false, DwarfExprEval::Completion::kAsync, 0,
             DwarfExprEval::ResultType::kPointer, "Invalid opcode 0xe1 in DWARF expression.");
}

// Tests the special opcodes that also encode a 0-31 literal.
TEST_F(DwarfExprEvalTest, LiteralOp) {
  DoEvalTest({llvm::dwarf::DW_OP_lit4}, true, DwarfExprEval::Completion::kSync, 4u,
             DwarfExprEval::ResultType::kPointer);
}

// Tests that reading fixed-length constant without enough room fails.
TEST_F(DwarfExprEvalTest, Const4ReadOffEnd) {
  DoEvalTest({llvm::dwarf::DW_OP_const4u, 0xf0}, false, DwarfExprEval::Completion::kSync, 0,
             DwarfExprEval::ResultType::kPointer, "Bad number format in DWARF expression.");
}

// Tests that reading a ULEB number without enough room fails.
TEST_F(DwarfExprEvalTest, ConstReadOffEnd) {
  // Note that LLVM allows LEB numbers to run off the end, and in that case
  // just stops reading data and reports the bits read.
  DoEvalTest({llvm::dwarf::DW_OP_constu}, false, DwarfExprEval::Completion::kSync, 0,
             DwarfExprEval::ResultType::kPointer, "Bad number format in DWARF expression.");
}

TEST_F(DwarfExprEvalTest, Addr) {
  // This encodes the relative address 0x4000.
  DoEvalTest({llvm::dwarf::DW_OP_addr, 0, 0x40, 0, 0, 0, 0, 0, 0}, true,
             DwarfExprEval::Completion::kSync, kModuleBase + 0x4000,
             DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Breg) {
  provider()->AddRegisterValue(kDWARFReg0ID, true, 100);
  provider()->AddRegisterValue(kDWARFReg9ID, false, 200);

  // reg0 (=100) + 129 = 229 (synchronous).
  // Note: 129 in SLEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_breg0, 0x81, 0x01}, true, DwarfExprEval::Completion::kSync, 229u,
             DwarfExprEval::ResultType::kPointer);
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());

  // reg9 (=200) - 127 = 73 (asynchronous).
  // -127 in SLEB is 0x81, 0x7f (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_breg9, 0x81, 0x7f}, true, DwarfExprEval::Completion::kAsync, 73u,
             DwarfExprEval::ResultType::kPointer);
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Bregx) {
  provider()->AddRegisterValue(kDWARFReg0ID, true, 100);
  provider()->AddRegisterValue(kDWARFReg9ID, false, 200);

  // reg0 (=100) + 129 = 229 (synchronous).
  // Note: 129 in SLEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_bregx, 0x00, 0x81, 0x01}, true, DwarfExprEval::Completion::kSync,
             229u, DwarfExprEval::ResultType::kPointer);
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());  // Because there's an offset.
  EXPECT_FALSE(eval().result_is_constant());

  // reg9 (=200) - 127 = 73 (asynchronous).
  // -127 in SLEB is 0x81, 0x7f (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_bregx, 0x09, 0x81, 0x7f}, true, DwarfExprEval::Completion::kAsync,
             73u, DwarfExprEval::ResultType::kPointer);
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());  // Because there's an offset.
  EXPECT_FALSE(eval().result_is_constant());

  // No offset should report the register source.
  // reg0 (=100) + 0 = 100 (synchronous).
  DoEvalTest({llvm::dwarf::DW_OP_bregx, 0x00, 0x00}, true, DwarfExprEval::Completion::kSync, 100u,
             DwarfExprEval::ResultType::kPointer);
  EXPECT_EQ(RegisterID::kARMv8_x0, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, CFA) {
  constexpr uint64_t kCFA = 0xdeadbeef;
  provider()->set_cfa(kCFA);

  // Most expressions involving the CFA are just the CFA itself (GCC likes
  // to declare the function frame base as being equal to the CFA).
  DoEvalTest({llvm::dwarf::DW_OP_call_frame_cfa}, true, DwarfExprEval::Completion::kSync, kCFA,
             DwarfExprEval::ResultType::kPointer);
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const1s) {
  DoEvalTest({llvm::dwarf::DW_OP_const1s, static_cast<uint8_t>(-3)}, true,
             DwarfExprEval::Completion::kSync, static_cast<DwarfExprEval::StackEntry>(-3),
             DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const1u) {
  DoEvalTest({llvm::dwarf::DW_OP_const1u, 0xf0}, true, DwarfExprEval::Completion::kSync, 0xf0,
             DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const2s) {
  DoEvalTest({llvm::dwarf::DW_OP_const2s, static_cast<uint8_t>(-3), 0xff}, true,
             DwarfExprEval::Completion::kSync, static_cast<DwarfExprEval::StackEntry>(-3),
             DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Const2u) {
  DoEvalTest({llvm::dwarf::DW_OP_const2u, 0x01, 0xf0}, true, DwarfExprEval::Completion::kSync,
             0xf001, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const4s) {
  DoEvalTest({llvm::dwarf::DW_OP_const4s, static_cast<uint8_t>(-3), 0xff, 0xff, 0xff}, true,
             DwarfExprEval::Completion::kSync, static_cast<DwarfExprEval::StackEntry>(-3),
             DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const4u) {
  DoEvalTest({llvm::dwarf::DW_OP_const4u, 0x03, 0x02, 0x01, 0xf0}, true,
             DwarfExprEval::Completion::kSync, 0xf0010203, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const8s) {
  DoEvalTest({llvm::dwarf::DW_OP_const8s, static_cast<uint8_t>(-3), 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff},
             true, DwarfExprEval::Completion::kSync, static_cast<DwarfExprEval::StackEntry>(-3),
             DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const8u) {
  DoEvalTest({llvm::dwarf::DW_OP_const8u, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0xf0}, true,
             DwarfExprEval::Completion::kSync, 0xf001020304050607u,
             DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Consts) {
  // -127 in SLEB is 0x81, 0x7f (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_consts, 0x81, 0x7f}, true, DwarfExprEval::Completion::kSync,
             static_cast<DwarfExprEval::StackEntry>(-127), DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

// Tests both "constu" and "drop".
TEST_F(DwarfExprEvalTest, ConstuDrop) {
  // 129 in ULEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest(
      {llvm::dwarf::DW_OP_constu, 0x81, 0x01, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_drop},
      true, DwarfExprEval::Completion::kSync, 129u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

// Tests both "dup" and "add".
TEST_F(DwarfExprEvalTest, DupAdd) {
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_dup, llvm::dwarf::DW_OP_plus}, true,
             DwarfExprEval::Completion::kSync, 16u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Neg) {
  // Negate one should give -1 casted to unsigned.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_neg}, true,
             DwarfExprEval::Completion::kSync, static_cast<DwarfExprEval::StackEntry>(-1),
             DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());

  // Double negate should come back to 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_neg, llvm::dwarf::DW_OP_neg}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Not) {
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_not}, true,
             DwarfExprEval::Completion::kSync, ~static_cast<DwarfExprEval::StackEntry>(1),
             DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Or) {
  // 8 | 1 = 9.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_or}, true,
             DwarfExprEval::Completion::kSync, 9u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Mul) {
  // 8 * 9 = 72.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit9, llvm::dwarf::DW_OP_mul}, true,
             DwarfExprEval::Completion::kSync, 72u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Minus) {
  // 8 - 2 = 6.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_minus}, true,
             DwarfExprEval::Completion::kSync, 6u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Over) {
  // Stack of (1, 2), this pushes "1" on the top.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_over}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());

  // Same operation with a drop to check the next-to-top item.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_over,
              llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, 2u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Pick) {
  // Stack of 1, 2, 3. Pick 0 -> 3.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_pick, 0},
             true, DwarfExprEval::Completion::kSync, 3u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());

  // Stack of 1, 2, 3. Pick 2 -> 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_pick, 2},
             true, DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());

  // Stack of 1, 2, 3. Pick 3 -> error.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_pick, 3},
             false, DwarfExprEval::Completion::kSync, 0u, DwarfExprEval::ResultType::kPointer,
             "Stack underflow for DWARF expression.");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Swap) {
  // 1, 2, swap -> 2, 1
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_swap}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_swap,
              llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, 2u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Rot) {
  // 1, 2, 3, rot -> 3, 1, 2 (test with 0, 1, and 2 "drops" to check all 3
  // stack elements).
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_rot},
             true, DwarfExprEval::Completion::kSync, 2u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_rot, llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_rot, llvm::dwarf::DW_OP_drop, llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, 3u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Abs) {
  // Abs of 1 -> 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_abs}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());

  // Abs of -1 -> 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_neg, llvm::dwarf::DW_OP_abs}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, And) {
  // 3 (=0b11) & 5 (=0b101) = 1
  DoEvalTest({llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_lit5, llvm::dwarf::DW_OP_and}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Div) {
  // 8 / -2 = -4.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_neg,
              llvm::dwarf::DW_OP_div},
             true, DwarfExprEval::Completion::kSync, static_cast<DwarfExprEval::StackEntry>(-4),
             DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());

  // Divide by zero should give an error.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_div}, false,
             DwarfExprEval::Completion::kSync, 0, DwarfExprEval::ResultType::kPointer,
             "DWARF expression divided by zero.");
}

TEST_F(DwarfExprEvalTest, Mod) {
  // 7 % 2 = 1
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_mod}, true,
             DwarfExprEval::Completion::kSync, 1, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());

  // Modulo 0 should give an error
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_mod}, false,
             DwarfExprEval::Completion::kSync, 0, DwarfExprEval::ResultType::kPointer,
             "DWARF expression divided by zero.");
}

TEST_F(DwarfExprEvalTest, PlusUconst) {
  // 7 + 129 = 136. 129 in ULEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_plus_uconst, 0x81, 0x01}, true,
             DwarfExprEval::Completion::kSync, 136u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Shr) {
  // 8 >> 1 = 4
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_shr}, true,
             DwarfExprEval::Completion::kSync, 4u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Shra) {
  // -7 (=0b1111...1111001) >> 2 = -2 (=0b1111...1110)
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_neg, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_shra},
             true, DwarfExprEval::Completion::kSync, static_cast<DwarfExprEval::StackEntry>(-2),
             DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Shl) {
  // 8 << 1 = 16
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_shl}, true,
             DwarfExprEval::Completion::kSync, 16u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Xor) {
  // 7 (=0b111) ^ 9 (=0b1001) = 14 (=0b1110)
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_lit9, llvm::dwarf::DW_OP_xor}, true,
             DwarfExprEval::Completion::kSync, 14u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Skip) {
  // Skip 0 (execute next instruction which just gives a constant).
  DoEvalTest({llvm::dwarf::DW_OP_skip, 0, 0, llvm::dwarf::DW_OP_lit9}, true,
             DwarfExprEval::Completion::kSync, 9u, DwarfExprEval::ResultType::kPointer);

  // Skip 1 (skip over user-defined instruction which would normally give an
  // error).
  DoEvalTest({llvm::dwarf::DW_OP_skip, 1, 0, llvm::dwarf::DW_OP_lo_user, llvm::dwarf::DW_OP_lit9},
             true, DwarfExprEval::Completion::kSync, 9u, DwarfExprEval::ResultType::kPointer);

  // Skip to the end should just terminate the program. The result when nothing
  // is left on the stack is 0.
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_skip, 1, 0, llvm::dwarf::DW_OP_nop}, true,
             DwarfExprEval::Completion::kSync, 0, DwarfExprEval::ResultType::kPointer);

  // Skip before the beginning is an error.
  DoEvalTest({llvm::dwarf::DW_OP_skip, 0, 0xff}, false, DwarfExprEval::Completion::kSync, 0,
             DwarfExprEval::ResultType::kPointer, "DWARF expression skips out-of-bounds.");
}

TEST_F(DwarfExprEvalTest, Bra) {
  // 0 @ top of stack means don't take the branch. This jumps out of bounds
  // which should not be taken.
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_bra, 0xff, 0, llvm::dwarf::DW_OP_lit9},
             true, DwarfExprEval::Completion::kSync, 9u, DwarfExprEval::ResultType::kPointer);
  EXPECT_TRUE(eval().result_is_constant());

  // Nonzero means take the branch. This jumps over a user-defined instruction
  // which would give an error if executed.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_bra, 1, 0, llvm::dwarf::DW_OP_lo_user,
              llvm::dwarf::DW_OP_lit9},
             true, DwarfExprEval::Completion::kSync, 9u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Eq) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_eq}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_eq}, true,
             DwarfExprEval::Completion::kSync, 0u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Ge) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_ge}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_ge}, true,
             DwarfExprEval::Completion::kSync, 0u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_ge}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Gt) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_gt}, true,
             DwarfExprEval::Completion::kSync, 0u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_gt}, true,
             DwarfExprEval::Completion::kSync, 0u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_gt}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Le) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_le}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_le}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_le}, true,
             DwarfExprEval::Completion::kSync, 0u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Lt) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lt}, true,
             DwarfExprEval::Completion::kSync, 0u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lt}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Ne) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_ne}, true,
             DwarfExprEval::Completion::kSync, 0u, DwarfExprEval::ResultType::kPointer);
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_ne}, true,
             DwarfExprEval::Completion::kSync, 1u, DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Fbreg) {
  constexpr uint64_t kBase = 0x1000000;
  provider()->set_bp(kBase);

  DoEvalTest({llvm::dwarf::DW_OP_fbreg, 0}, true, DwarfExprEval::Completion::kSync, kBase,
             DwarfExprEval::ResultType::kPointer);
  EXPECT_FALSE(eval().result_is_constant());

  // Note: 129 in SLEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_fbreg, 0x81, 0x01}, true, DwarfExprEval::Completion::kSync,
             kBase + 129u, DwarfExprEval::ResultType::kPointer);
  EXPECT_FALSE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Deref) {
  // This is a real program Clang generated. 0x58 = -40 in SLEB128 so:
  //   *[reg6 - 40] - 0x30
  const std::vector<uint8_t> program = {llvm::dwarf::DW_OP_breg6,  0x58, llvm::dwarf::DW_OP_deref,
                                        llvm::dwarf::DW_OP_constu, 0x30, llvm::dwarf::DW_OP_minus};

  constexpr uint64_t kReg6 = 0x1000;
  provider()->AddRegisterValue(kDWARFReg6ID, true, kReg6);
  constexpr int64_t kOffsetFromReg6 = -40;

  // Contents of the data at [reg6 - 40]
  constexpr uint64_t kMemoryContents = 0x5000000000;
  std::vector<uint8_t> mem;
  mem.resize(sizeof(kMemoryContents));
  memcpy(&mem[0], &kMemoryContents, sizeof(kMemoryContents));
  provider()->AddMemory(kReg6 + kOffsetFromReg6, mem);

  DoEvalTest(program, true, DwarfExprEval::Completion::kAsync, kMemoryContents - 0x30,
             DwarfExprEval::ResultType::kPointer);
  EXPECT_FALSE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, DerefSize) {
  // This is a real program GCC generated.
  // This is "[BYTE PTR rdx] + 2"
  constexpr uint8_t kAddAmount = 2;
  const std::vector<uint8_t> program = {
      llvm::dwarf::DW_OP_breg1,       0,          llvm::dwarf::DW_OP_deref_size, 0x01,
      llvm::dwarf::DW_OP_plus_uconst, kAddAmount, llvm::dwarf::DW_OP_stack_value};

  constexpr uint64_t kReg1 = 0x1000;
  provider()->AddRegisterValue(kDWARFReg1ID, true, kReg1);

  // Contents of the data at [reg1]. We have the value and some other bytes following it to make
  // sure the correct number of bytes were read.
  constexpr uint8_t kMemValue = 0x50;
  std::vector<uint8_t> mem{kMemValue, 0xff, 0xff, 0xff, 0xff};
  provider()->AddMemory(kReg1, mem);

  DoEvalTest(program, true, DwarfExprEval::Completion::kAsync, kMemValue + kAddAmount,
             DwarfExprEval::ResultType::kValue);
  EXPECT_FALSE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, ImplicitValue) {
  // This is a real program GCC generated for the 80-bit constant 2.38. It encodes it as a 128-bit
  // constant for some reason.
  // clang-format off
  std::vector<uint8_t> program = {llvm::dwarf::DW_OP_implicit_value, 0x10,
                                  0x00, 0x50, 0xb8, 0x1e, 0x85, 0xeb, 0x51, 0x98,
                                  0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  constexpr uint128_t kExpected = (static_cast<uint128_t>(0x4000) << 64) | 0x9851eb851eb85000llu;
  DoEvalTest(program, true, DwarfExprEval::Completion::kSync, kExpected,
             DwarfExprEval::ResultType::kValue);
  EXPECT_TRUE(eval().result_is_constant());

  // This program declares it has 0x10 bytes of data (2nd array value), but there are only 0x0f
  // values following it.
  std::vector<uint8_t> bad_program = {llvm::dwarf::DW_OP_implicit_value, 0x10,
                                      0x00, 0x50, 0xb8, 0x1e, 0x85, 0xeb, 0x51, 0x98,
                                      0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
  // clang-format on
  DoEvalTest(bad_program, false, DwarfExprEval::Completion::kSync, kExpected,
             DwarfExprEval::ResultType::kValue, "DWARF implicit value length too long: 0x10.");
}

TEST_F(DwarfExprEvalTest, Piece_Value) {
  // This expression and the register and memory values were generated by GCC for this code with
  // "-O2":
  //
  //   int __attribute((noinline)) foo(int x, int y) {
  //     struct { int x, y; } s = {x, y};
  //     s.x *= 2;
  //     return s.x;
  //   }
  //
  // Structure definition:
  //   "x" offset = 0 (4 bytes long)
  //   "y" offset = 4 (4 bytes long)
  //
  // clang-format off
  std::vector<uint8_t> program{
    llvm::dwarf::DW_OP_breg3, 0,     // Original s.x is in "reg3"
    llvm::dwarf::DW_OP_lit1,
    llvm::dwarf::DW_OP_shl,          // reg3 << 1
    llvm::dwarf::DW_OP_stack_value,
    llvm::dwarf::DW_OP_piece, 0x04,  // Pick 4 bytes.
    llvm::dwarf::DW_OP_reg4,         // s.y is in "reg4".
    llvm::dwarf::DW_OP_piece, 0x04
  };
  // clang-format on

  provider()->AddRegisterValue(kDWARFReg3ID, true, 1);   // Original "x" value.
  provider()->AddRegisterValue(kDWARFReg4ID, true, 17);  // "y" value.

  DoEvalTest(program, true, DwarfExprEval::Completion::kSync, 0, DwarfExprEval::ResultType::kData);

  // Result should be {x = 2, y = 17}.
  std::vector<uint8_t> expected{0x02, 0, 0, 0, 0x11, 0, 0, 0};
  EXPECT_EQ(expected, eval().result_data());
}

TEST_F(DwarfExprEvalTest, Piece_Memory) {
  // This expression is made up based on the Piece_Value one to also incorporate a memory
  // dereference.
  // clang-format off
  std::vector<uint8_t> program{
    llvm::dwarf::DW_OP_breg3, 0,     // Original s.x is in "reg3"
    llvm::dwarf::DW_OP_lit1,
    llvm::dwarf::DW_OP_shl,          // reg3 << 1
    llvm::dwarf::DW_OP_stack_value,
    llvm::dwarf::DW_OP_piece, 0x04,  // Pick 4 bytes.
    llvm::dwarf::DW_OP_breg4, 0,     // DIFFERENT FROM ABOVE: s.y is pointed to "reg4".
    llvm::dwarf::DW_OP_piece, 0x04
  };
  // clang-format on

  // Data pointed to by "reg4".
  constexpr uint64_t kReg4Address = 0x87654321;
  std::vector<uint8_t> mem{0x11, 0, 0, 0};
  provider()->AddMemory(kReg4Address, mem);

  provider()->AddRegisterValue(kDWARFReg3ID, true, 1);             // Original "x" value.
  provider()->AddRegisterValue(kDWARFReg4ID, true, kReg4Address);  // Points to the "y" value.

  DoEvalTest(program, true, DwarfExprEval::Completion::kAsync, 0, DwarfExprEval::ResultType::kData);

  // Result should be {x = 2, y = 17}.
  std::vector<uint8_t> expected{0x02, 0, 0, 0, 0x11, 0, 0, 0};
  EXPECT_EQ(expected, eval().result_data());
}

}  // namespace zxdb
