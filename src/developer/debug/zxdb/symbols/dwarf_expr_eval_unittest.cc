// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"

#include <gtest/gtest.h>

#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_factory.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

namespace {

using debug::RegisterID;

// Base address of the imaginary module. Relative addresses will be relative to this number.
constexpr TargetPointer kModuleBase = 0x78000000;

// Offset of the compilation unit DIE corresponding to the DIE referencing the evaluated expression
// in the symbols. This is used by the UnitSymbolFactory when there are DIE references in an
// expression.
constexpr uint64_t kUnitOffset = 0x2000000;

class DwarfExprEvalTest : public TestWithLoop {
 public:
  DwarfExprEvalTest()
      : symbol_factory_(fxl::MakeRefCounted<MockSymbolFactory>()),
        provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()),
        eval_(UnitSymbolFactory(symbol_factory_, kUnitOffset), provider_, symbol_context_) {}

  DwarfExprEval& eval() { return eval_; }
  MockSymbolFactory& symbol_factory() { return *symbol_factory_; }
  fxl::RefPtr<MockSymbolDataProvider> provider() { return provider_; }
  const SymbolContext symbol_context() const { return symbol_context_; }

  // The expected_string is the stringified version of this expression.
  //
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
                  DwarfExprEval::Completion expected_completion,
                  const DwarfStackEntry& expected_result,
                  DwarfExprEval::ResultType expected_result_type, const char* expected_string,
                  const char* expected_message = nullptr);

  // Same as the above but takes a DwarfExpr.
  void DoEvalTest(DwarfExpr expr, bool expected_success,
                  DwarfExprEval::Completion expected_completion,
                  const DwarfStackEntry& expected_result,
                  DwarfExprEval::ResultType expected_result_type, const char* expected_string,
                  const char* expected_message = nullptr);

  // Just does the evaluation part of the eval test. This does not check the stringified version
  // of the expression, and does not clear any previous state of the evaluator.
  //
  // Tests can use this variant if they want to set up some stack entries manually and then run an
  // expression based on those.
  void DoEval(DwarfExpr expr, bool expected_success, DwarfExprEval::Completion expected_completion,
              const DwarfStackEntry& expected_result,
              DwarfExprEval::ResultType expected_result_type,
              const char* expected_message = nullptr);

 private:
  fxl::RefPtr<MockSymbolFactory> symbol_factory_;
  fxl::RefPtr<MockSymbolDataProvider> provider_;
  SymbolContext symbol_context_ = SymbolContext(kModuleBase);
  DwarfExprEval eval_;
};

void DwarfExprEvalTest::DoEvalTest(const std::vector<uint8_t> data, bool expected_success,
                                   DwarfExprEval::Completion expected_completion,
                                   const DwarfStackEntry& expected_result,
                                   DwarfExprEval::ResultType expected_result_type,
                                   const char* expected_string, const char* expected_message) {
  DoEvalTest(DwarfExpr(data), expected_success, expected_completion, expected_result,
             expected_result_type, expected_string, expected_message);
}

void DwarfExprEvalTest::DoEvalTest(DwarfExpr expr, bool expected_success,
                                   DwarfExprEval::Completion expected_completion,
                                   const DwarfStackEntry& expected_result,
                                   DwarfExprEval::ResultType expected_result_type,
                                   const char* expected_string, const char* expected_message) {
  // Check string-ification. Do this first because it won't set up the complete state of the
  // DwarfExprEval and some tests want to validate this after the DoEvalTest call.
  eval_.Clear();
  std::string stringified = eval_.ToString(expr, false);
  EXPECT_EQ(expected_string, stringified);

  eval_.Clear();
  DoEval(expr, expected_success, expected_completion, expected_result, expected_result_type,
         expected_message);
}

void DwarfExprEvalTest::DoEval(DwarfExpr expr, bool expected_success,
                               DwarfExprEval::Completion expected_completion,
                               const DwarfStackEntry& expected_result,
                               DwarfExprEval::ResultType expected_result_type,
                               const char* expected_message) {
  bool callback_issued = false;
  EXPECT_EQ(
      expected_completion,
      eval_.Eval(expr, [&callback_issued, expected_success, expected_result, expected_result_type,
                        expected_message](DwarfExprEval* eval, const Err& err) {
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
      }));

  if (expected_completion == DwarfExprEval::Completion::kAsync) {
    // In the async case the message loop needs to be run to get the result.
    EXPECT_FALSE(eval_.is_complete());
    EXPECT_FALSE(callback_issued);

    // Ensure the callback was made after running the loop.
    loop().RunUntilNoTasks();
  }

  EXPECT_TRUE(eval_.is_complete());
  EXPECT_TRUE(callback_issued);
}

const debug::RegisterID kDWARFReg0ID = debug::RegisterID::kARMv8_x0;
const debug::RegisterID kDWARFReg1ID = debug::RegisterID::kARMv8_x1;
const debug::RegisterID kDWARFReg3ID = debug::RegisterID::kARMv8_x3;
const debug::RegisterID kDWARFReg4ID = debug::RegisterID::kARMv8_x4;
const debug::RegisterID kDWARFReg5ID = debug::RegisterID::kARMv8_x5;
const debug::RegisterID kDWARFReg6ID = debug::RegisterID::kARMv8_x6;
const debug::RegisterID kDWARFReg9ID = debug::RegisterID::kARMv8_x9;

}  // namespace

TEST_F(DwarfExprEvalTest, NoResult) {
  const char kNoResults[] = "DWARF expression produced no results.";

  // Empty expression.
  DoEvalTest(DwarfExpr(), false, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kPointer, "", kNoResults);
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_TRUE(eval().result_is_constant());

  // Nonempty expression that produces no results.
  DoEvalTest({llvm::dwarf::DW_OP_nop}, false, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kPointer, "DW_OP_nop", kNoResults);
}

TEST_F(DwarfExprEvalTest, MarkValue) {
  // A computation without "stack_value" should report the result type as a pointers.
  DoEvalTest({llvm::dwarf::DW_OP_lit4}, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(4u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit4");
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_TRUE(eval().result_is_constant());

  // "stack value" should mark the result as a stack value.
  DoEvalTest({llvm::dwarf::DW_OP_lit4, llvm::dwarf::DW_OP_stack_value}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(4u),
             DwarfExprEval::ResultType::kValue, "DW_OP_lit4, DW_OP_stack_value");
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_TRUE(eval().result_is_constant());
}

// Tests that we can recover from infinite loops and destroy the evaluator when it's got an
// asynchronous operation pending.
TEST_F(DwarfExprEvalTest, InfiniteLoop) {
  // This expression loops back to the beginning infinitely.
  std::vector<uint8_t> loop_data = {llvm::dwarf::DW_OP_skip, 0xfd, 0xff};

  std::unique_ptr<DwarfExprEval> eval =
      std::make_unique<DwarfExprEval>(UnitSymbolFactory(), provider(), symbol_context());

  bool callback_issued = false;
  eval->Eval(DwarfExpr(loop_data),
             [&callback_issued](DwarfExprEval* eval, const Err& err) { callback_issued = true; });

  // Let the message loop process messages for a few times so the evaluator can run.
  loop().PostTask(FROM_HERE, []() { debug::MessageLoop::Current()->QuitNow(); });
  loop().Run();
  loop().PostTask(FROM_HERE, []() { debug::MessageLoop::Current()->QuitNow(); });
  loop().Run();

  // Reset the evaluator, this should cancel everything.
  eval.reset();

  // This should not crash (the evaluator may have posted a pending task that will get executed when
  // we run the loop again, and it should notice the object is gone).
  loop().PostTask(FROM_HERE, []() { debug::MessageLoop::Current()->QuitNow(); });
  loop().Run();

  // Callback should never have been issued.
  EXPECT_FALSE(callback_issued);
}

// Tests synchronously reading a single register.
TEST_F(DwarfExprEvalTest, SyncRegister) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);

  DoEvalTest({llvm::dwarf::DW_OP_reg0}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(kValue), DwarfExprEval::ResultType::kValue, "DW_OP_reg0");
  EXPECT_EQ(RegisterID::kARMv8_x0, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

// Tests the encoding form of registers as parameters to an operation rather than the version
// encoded in the operation.
//
// Also tests DW_OP_nop.
TEST_F(DwarfExprEvalTest, SyncRegisterAsNumber) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg1ID, true, kValue);

  // Use "regx" which will read the register number as a ULEB following it.
  // The byte is the ULEB-encoded version of 1 (high bit set to 0 indicate it's the last byte).
  std::vector<uint8_t> expr_data;
  expr_data.push_back(llvm::dwarf::DW_OP_nop);
  expr_data.push_back(llvm::dwarf::DW_OP_regx);
  expr_data.push_back(0b00000001);

  DoEvalTest(expr_data, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(kValue),
             DwarfExprEval::ResultType::kValue, "DW_OP_nop, DW_OP_regx(1)");
  EXPECT_EQ(RegisterID::kARMv8_x1, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

// Tests asynchronously reading a single register.
TEST_F(DwarfExprEvalTest, AsyncRegister) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);

  DoEvalTest({llvm::dwarf::DW_OP_reg0}, true, DwarfExprEval::Completion::kAsync,
             DwarfStackEntry(kValue), DwarfExprEval::ResultType::kValue, "DW_OP_reg0");
  EXPECT_EQ(RegisterID::kARMv8_x0, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

// Tests synchronously hitting an invalid opcode.
TEST_F(DwarfExprEvalTest, SyncInvalidOp) {
  // Make a program that consists only of a user-defined opcode (not supported). Can't use
  // DW_OP_lo_user because that's a GNU TLS extension we know about.
  DoEvalTest({llvm::dwarf::DW_OP_lo_user + 1}, false, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(0), DwarfExprEval::ResultType::kValue, "INVALID_OPCODE(0xe1)",
             "Invalid opcode 0xe1 in DWARF expression.");
}

// Tests synchronously hitting an invalid opcode (async error handling).
TEST_F(DwarfExprEvalTest, AsyncInvalidOp) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);

  // Make a program that consists of getting an async register and then executing an invalid opcode.
  // Can't use DW_OP_lo_user because that's a GNU TLS extension we know about.
  std::vector<uint8_t> expr_data;
  expr_data.push_back(llvm::dwarf::DW_OP_reg0);
  expr_data.push_back(llvm::dwarf::DW_OP_lo_user + 1);

  DoEvalTest(expr_data, false, DwarfExprEval::Completion::kAsync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kPointer, "DW_OP_reg0, INVALID_OPCODE(0xe1)",
             "Invalid opcode 0xe1 in DWARF expression.");
}

// Tests the special opcodes that also encode a 0-31 literal.
TEST_F(DwarfExprEvalTest, LiteralOp) {
  DoEvalTest({llvm::dwarf::DW_OP_lit4}, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(4u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit4");
}

// Tests that reading fixed-length constant without enough room fails.
TEST_F(DwarfExprEvalTest, Const4ReadOffEnd) {
  DoEvalTest({llvm::dwarf::DW_OP_const4u, 0xf0}, false, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(0), DwarfExprEval::ResultType::kPointer,
             "ERROR: \"Bad number format in DWARF expression.\"",
             "Bad number format in DWARF expression.");
}

// Tests that reading a ULEB number without enough room fails.
TEST_F(DwarfExprEvalTest, ConstReadOffEnd) {
  // Note that LLVM allows LEB numbers to run off the end, and in that case just stops reading data
  // and reports the bits read.
  DoEvalTest({llvm::dwarf::DW_OP_constu}, false, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(0), DwarfExprEval::ResultType::kPointer,
             "ERROR: \"Bad number format in DWARF expression.\"",
             "Bad number format in DWARF expression.");
}

TEST_F(DwarfExprEvalTest, Addr) {
  // This encodes the relative address 0x4000.
  DoEvalTest({llvm::dwarf::DW_OP_addr, 0, 0x40, 0, 0, 0, 0, 0, 0}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(kModuleBase + 0x4000),
             DwarfExprEval::ResultType::kPointer, "DW_OP_addr(0x4000)");
}

TEST_F(DwarfExprEvalTest, AddrxAndConstx) {
  // These definitions depend on .debug_addr data which is provided by a ModuleSymbols.
  auto module_symbols = fxl::MakeRefCounted<MockModuleSymbols>("file.exe");

  // This unit has an DW_AT_addr_base which is added to the offsets for the "addrx" and "constx"
  // operators for expressions inside of it.
  constexpr uint64_t kAddrBase = 12;
  auto compile_unit =
      fxl::MakeRefCounted<CompileUnit>(module_symbols->GetWeakPtr(), fxl::RefPtr<DwarfUnit>(),
                                       DwarfLang::kCpp14, "source.cc", kAddrBase);

  // Offset from kAddrBase of our value.
  constexpr uint8_t kOffset = 8;

  // The value of the .debug_addr entry referenced by the variable.
  constexpr uint64_t kAddr = 0x12345678;
  module_symbols->AddDebugAddrEntry(kAddrBase, kOffset, kAddr);

  // The variable our expression will be associated with. This variable doesn't have to actually
  // have a type or the location expression we're using, it just needs to reference the compilation
  // unit which has the addr_base and references the mock module symbols.
  auto var =
      fxl::MakeRefCounted<Variable>(DwarfTag::kVariable, "var", LazySymbol(), VariableLocation());
  SymbolTestParentSetter var_parent_setter(var, compile_unit);  // Link compile unit to the parent.

  // Since the var doesn't actrually reference this expression, we don't need to worry about
  // reference cycles. If in the future we need to reference the DwarfExpr from the var above,
  // we'll need to manually clear the DwarfExpr's source to prevent a leak.
  DwarfExpr addrx_expr({llvm::dwarf::DW_OP_addrx, kOffset}, UncachedLazySymbol::MakeUnsafe(var));

  // The "addrx" expression should read the kAddr value from the .debug_addr table at the location
  // we set up, and then relocate it relative to the module's base address.
  DoEvalTest(addrx_expr, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(kModuleBase + kAddr), DwarfExprEval::ResultType::kPointer,
             "DW_OP_addrx(8, with addr_base=0xc) -> rel=0x12345678, abs=0x8a345678");

  // Same test with "constx". This is the same except the resulting address is not relocated from
  // the module base.
  //
  // Note: I have not actually seen this operator in use. This expected behavior is based only on my
  // reading of the spec.
  DwarfExpr constx_expr({llvm::dwarf::DW_OP_constx, kOffset}, UncachedLazySymbol::MakeUnsafe(var));
  DoEvalTest(constx_expr, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(kAddr),
             DwarfExprEval::ResultType::kValue,
             "DW_OP_constx(8, with addr_base=0xc) -> 0x12345678");

  // Same test with an invalid address offset.
  DwarfExpr invalid_expr({llvm::dwarf::DW_OP_constx, 16}, UncachedLazySymbol::MakeUnsafe(var));
  DoEvalTest(invalid_expr, false, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kPointer,
             "ERROR: \"Unable to read .debug_addr section to evaluate expression.\"");
}

TEST_F(DwarfExprEvalTest, Breg) {
  provider()->AddRegisterValue(kDWARFReg0ID, true, 100);
  provider()->AddRegisterValue(kDWARFReg9ID, false, 200);

  // reg0 (=100) + 129 = 229 (synchronous).
  // Note: 129 in SLEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_breg0, 0x81, 0x01}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(229u), DwarfExprEval::ResultType::kPointer, "DW_OP_breg0(129)");
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());

  // reg9 (=200) - 127 = 73 (asynchronous).
  // -127 in SLEB is 0x81, 0x7f (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_breg9, 0x81, 0x7f}, true, DwarfExprEval::Completion::kAsync,
             DwarfStackEntry(73u), DwarfExprEval::ResultType::kPointer, "DW_OP_breg9(-127)");
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Bregx) {
  provider()->AddRegisterValue(kDWARFReg0ID, true, 100);
  provider()->AddRegisterValue(kDWARFReg9ID, false, 200);

  // reg0 (=100) + 129 = 229 (synchronous).
  // Note: 129 in SLEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_bregx, 0x00, 0x81, 0x01}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(229u), DwarfExprEval::ResultType::kPointer, "DW_OP_bregx(0, 129)");
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());  // Because there's an offset.
  EXPECT_FALSE(eval().result_is_constant());

  // reg9 (=200) - 127 = 73 (asynchronous).
  // -127 in SLEB is 0x81, 0x7f (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_bregx, 0x09, 0x81, 0x7f}, true, DwarfExprEval::Completion::kAsync,
             DwarfStackEntry(73u), DwarfExprEval::ResultType::kPointer, "DW_OP_bregx(9, -127)");
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());  // Because there's an offset.
  EXPECT_FALSE(eval().result_is_constant());

  // No offset should report the register source.
  // reg0 (=100) + 0 = 100 (synchronous).
  DoEvalTest({llvm::dwarf::DW_OP_bregx, 0x00, 0x00}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(100u), DwarfExprEval::ResultType::kPointer, "DW_OP_bregx(0, 0)");
  EXPECT_EQ(RegisterID::kARMv8_x0, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, CFA) {
  constexpr uint64_t kCFA = 0xdeadbeef;
  provider()->set_cfa(kCFA);

  // Most expressions involving the CFA are just the CFA itself (GCC likes
  // to declare the function frame base as being equal to the CFA).
  DoEvalTest({llvm::dwarf::DW_OP_call_frame_cfa}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(kCFA), DwarfExprEval::ResultType::kPointer, "DW_OP_call_frame_cfa");
  EXPECT_EQ(RegisterID::kUnknown, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const1s) {
  DoEvalTest({llvm::dwarf::DW_OP_const1s, static_cast<uint8_t>(-3)}, true,
             DwarfExprEval::Completion::kSync,
             DwarfStackEntry(static_cast<DwarfStackEntry::UnsignedType>(-3)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_const1s(-3)");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const1u) {
  DoEvalTest({llvm::dwarf::DW_OP_const1u, 0xf0}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(0xf0), DwarfExprEval::ResultType::kPointer, "DW_OP_const1u(240)");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const2s) {
  DoEvalTest({llvm::dwarf::DW_OP_const2s, static_cast<uint8_t>(-3), 0xff}, true,
             DwarfExprEval::Completion::kSync,
             DwarfStackEntry(static_cast<DwarfStackEntry::UnsignedType>(-3)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_const2s(-3)");
}

TEST_F(DwarfExprEvalTest, Const2u) {
  DoEvalTest({llvm::dwarf::DW_OP_const2u, 0x01, 0xf0}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(0xf001), DwarfExprEval::ResultType::kPointer, "DW_OP_const2u(0xf001)");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const4s) {
  DoEvalTest({llvm::dwarf::DW_OP_const4s, static_cast<uint8_t>(-3), 0xff, 0xff, 0xff}, true,
             DwarfExprEval::Completion::kSync,
             DwarfStackEntry(static_cast<DwarfStackEntry::UnsignedType>(-3)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_const4s(-3)");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const4u) {
  DoEvalTest({llvm::dwarf::DW_OP_const4u, 0x03, 0x02, 0x01, 0xf0}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0xf0010203),
             DwarfExprEval::ResultType::kPointer, "DW_OP_const4u(0xf0010203)");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const8s) {
  DoEvalTest({llvm::dwarf::DW_OP_const8s, static_cast<uint8_t>(-3), 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff},
             true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(static_cast<DwarfStackEntry::UnsignedType>(-3)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_const8s(-3)");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Const8u) {
  DoEvalTest({llvm::dwarf::DW_OP_const8u, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0xf0}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0xf001020304050607u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_const8u(0xf001020304050607)");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Consts) {
  // -127 in SLEB is 0x81, 0x7f (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_consts, 0x81, 0x7f}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(static_cast<DwarfStackEntry::UnsignedType>(-127)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_consts(-127)");
  EXPECT_TRUE(eval().result_is_constant());
}

// Tests both "constu" and "drop".
TEST_F(DwarfExprEvalTest, ConstuDrop) {
  // 129 in ULEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest(
      {llvm::dwarf::DW_OP_constu, 0x81, 0x01, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_drop},
      true, DwarfExprEval::Completion::kSync, DwarfStackEntry(129u),
      DwarfExprEval::ResultType::kPointer, "DW_OP_constu(129), DW_OP_lit0, DW_OP_drop");
  EXPECT_TRUE(eval().result_is_constant());
}

// Tests both "dup" and "add".
TEST_F(DwarfExprEvalTest, DupAdd) {
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_dup, llvm::dwarf::DW_OP_plus}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(16u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit8, DW_OP_dup, DW_OP_plus");
  EXPECT_TRUE(eval().result_is_constant());

  // Adding two different types fails, this is an unsigned integer and a generic type.
  auto uint_type = MakeUint32Type();
  eval().Clear();
  eval().Push(DwarfStackEntry(uint_type, uint128_t(17u)));
  eval().Push(DwarfStackEntry(10u));
  DoEval(DwarfExpr({llvm::dwarf::DW_OP_plus}), false, DwarfExprEval::Completion::kSync,
         DwarfStackEntry(0u), DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Neg) {
  // Negate one should give -1 casted to unsigned.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_neg}, true,
             DwarfExprEval::Completion::kSync,
             DwarfStackEntry(static_cast<DwarfStackEntry::UnsignedType>(-1)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_neg");
  EXPECT_TRUE(eval().result_is_constant());

  // Double negate should come back to 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_neg, llvm::dwarf::DW_OP_neg}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_neg, DW_OP_neg");
  EXPECT_TRUE(eval().result_is_constant());

  // Negating a specifically unsigned value is a no-op. I have no idea if this is what DWARF wants
  // (the spec isn't very specific). With this test we at least clearly define our behavior and
  // make sure that if it changes, we do so on purpose.
  auto uint_type = MakeUint32Type();
  eval().Clear();
  eval().Push(DwarfStackEntry(uint_type, uint128_t(17u)));
  DoEval(DwarfExpr({llvm::dwarf::DW_OP_neg}), true, DwarfExprEval::Completion::kSync,
         DwarfStackEntry(uint_type, uint128_t(17u)), DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Not) {
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_not}, true,
             DwarfExprEval::Completion::kSync,
             DwarfStackEntry(~static_cast<DwarfStackEntry::UnsignedType>(1)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_not");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Or) {
  // 8 | 1 = 9.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_or}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(9u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit8, DW_OP_lit1, DW_OP_or");
  EXPECT_TRUE(eval().result_is_constant());

  // Typed as a float should fail (can't "or" non-integral types).
  auto float_type = MakeFloatType();
  eval().Clear();
  eval().Push(DwarfStackEntry(float_type, 8.0f));
  eval().Push(DwarfStackEntry(float_type, 1.0f));
  DoEval(DwarfExpr({llvm::dwarf::DW_OP_or}), false, DwarfExprEval::Completion::kSync,
         DwarfStackEntry(0u), DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Mul) {
  // 8 * 9 = 72.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit9, llvm::dwarf::DW_OP_mul}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(72u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit8, DW_OP_lit9, DW_OP_mul");
  EXPECT_TRUE(eval().result_is_constant());

  // Typed as a float.
  auto float_type = MakeFloatType();
  eval().Clear();
  eval().Push(DwarfStackEntry(float_type, -8.712f));
  eval().Push(DwarfStackEntry(float_type, 67.991f));
  DoEval(DwarfExpr({llvm::dwarf::DW_OP_mul}), true, DwarfExprEval::Completion::kSync,
         DwarfStackEntry(float_type, -592.3376f), DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Minus) {
  // 8 - 2 = 6.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_minus}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(6u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit8, DW_OP_lit2, DW_OP_minus");
  EXPECT_TRUE(eval().result_is_constant());

  // Typed as a double.
  auto double_type = MakeDoubleType();
  eval().Clear();
  eval().Push(DwarfStackEntry(double_type, 8.712));
  eval().Push(DwarfStackEntry(double_type, 67.991));
  DoEval(DwarfExpr({llvm::dwarf::DW_OP_minus}), true, DwarfExprEval::Completion::kSync,
         DwarfStackEntry(double_type, -59.279), DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Over) {
  // Stack of (1, 2), this pushes "1" on the top.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_over}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_lit2, DW_OP_over");
  EXPECT_TRUE(eval().result_is_constant());

  // Same operation with a drop to check the next-to-top item.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_over,
              llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(2u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_lit2, DW_OP_over, DW_OP_drop");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Pick) {
  // Stack of 1, 2, 3. Pick 0 -> 3.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_pick, 0},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(3u),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_lit1, DW_OP_lit2, DW_OP_lit3, DW_OP_pick(0)");
  EXPECT_TRUE(eval().result_is_constant());

  // Stack of 1, 2, 3. Pick 2 -> 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_pick, 2},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_lit1, DW_OP_lit2, DW_OP_lit3, DW_OP_pick(2)");
  EXPECT_TRUE(eval().result_is_constant());

  // Stack of 1, 2, 3. Pick 3 -> error.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_pick, 3},
             false, DwarfExprEval::Completion::kSync, DwarfStackEntry(0u),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_lit1, DW_OP_lit2, DW_OP_lit3, DW_OP_pick(3)",
             "Stack underflow for DWARF expression.");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Swap) {
  // 1, 2, swap -> 2, 1
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_swap}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_lit2, DW_OP_swap");
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_swap,
              llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(2u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_lit2, DW_OP_swap, DW_OP_drop");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Rot) {
  // 1, 2, 3, rot -> 3, 1, 2 (test with 0, 1, and 2 "drops" to check all 3
  // stack elements).
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_rot},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(2u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_lit2, DW_OP_lit3, DW_OP_rot");
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_rot, llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_lit1, DW_OP_lit2, DW_OP_lit3, DW_OP_rot, DW_OP_drop");
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_lit3,
              llvm::dwarf::DW_OP_rot, llvm::dwarf::DW_OP_drop, llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(3u),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_lit1, DW_OP_lit2, DW_OP_lit3, DW_OP_rot, DW_OP_drop, DW_OP_drop");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Abs) {
  // Abs of 1 -> 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_abs}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_abs");
  EXPECT_TRUE(eval().result_is_constant());

  // Abs of -1 -> 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_neg, llvm::dwarf::DW_OP_abs}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_neg, DW_OP_abs");

  // Absolute value of an explicitly unsigned type is a no-op (this value is picked so it will
  // behave differently if the code treats it as signed.
  auto uint_type = MakeUint32Type();
  DwarfStackEntry stack_val(uint_type, static_cast<DwarfStackEntry::UnsignedType>(-5));
  eval().Clear();
  eval().Push(stack_val);
  DoEval(DwarfExpr({llvm::dwarf::DW_OP_neg}), true, DwarfExprEval::Completion::kSync, stack_val,
         DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, And) {
  // 3 (=0b11) & 5 (=0b101) = 1
  DoEvalTest({llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_lit5, llvm::dwarf::DW_OP_and}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit3, DW_OP_lit5, DW_OP_and");
  EXPECT_TRUE(eval().result_is_constant());
}

TEST_F(DwarfExprEvalTest, Div) {
  // 8 / -2 = -4.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_neg,
              llvm::dwarf::DW_OP_div},
             true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(static_cast<DwarfStackEntry::UnsignedType>(-4)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit8, DW_OP_lit2, DW_OP_neg, DW_OP_div");
  EXPECT_TRUE(eval().result_is_constant());

  // Divide by zero should give an error.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_div}, false,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit8, DW_OP_lit0, DW_OP_div",
             "Error evaluating DW_OP_div in DWARF expression: Divide by zero.");
}

TEST_F(DwarfExprEvalTest, Mod) {
  // 7 % 2 = 1
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_mod}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit7, DW_OP_lit2, DW_OP_mod");
  EXPECT_TRUE(eval().result_is_constant());

  // Modulo 0 should give an error
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_mod}, false,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit7, DW_OP_lit0, DW_OP_mod",
             "Error evaluating DW_OP_mod in DWARF expression: Divide by zero.");
}

TEST_F(DwarfExprEvalTest, PlusUconst) {
  // 7 + 129 = 136. 129 in ULEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_plus_uconst, 0x81, 0x01}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(136u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit7, DW_OP_plus_uconst(129)");
  EXPECT_TRUE(eval().result_is_constant());

  // The value should get reinterpreted as the same type as the top of the stack. This adds 129 to
  // a floating-point number.
  auto double_type = MakeDoubleType();
  eval().Clear();
  eval().Push(DwarfStackEntry(double_type, 3.14159));
  DoEval(DwarfExpr({llvm::dwarf::DW_OP_plus_uconst, 0x81, 0x01}), true,
         DwarfExprEval::Completion::kSync, DwarfStackEntry(double_type, 132.14159),
         DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Shr) {
  // 8 >> 1 = 4
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_shr}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(4u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit8, DW_OP_lit1, DW_OP_shr");
}

TEST_F(DwarfExprEvalTest, Shra) {
  // -7 (=0b1111...1111001) >> 2 = -2 (=0b1111...1110)
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_neg, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_shra},
             true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(static_cast<DwarfStackEntry::UnsignedType>(-2)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit7, DW_OP_neg, DW_OP_lit2, DW_OP_shra");
}

TEST_F(DwarfExprEvalTest, Shl) {
  // 8 << 1 = 16
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_shl}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(16u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit8, DW_OP_lit1, DW_OP_shl");
}

TEST_F(DwarfExprEvalTest, Xor) {
  // 7 (=0b111) ^ 9 (=0b1001) = 14 (=0b1110)
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_lit9, llvm::dwarf::DW_OP_xor}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(14u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit7, DW_OP_lit9, DW_OP_xor");
}

TEST_F(DwarfExprEvalTest, Skip) {
  // Note for these tests that execution evaluates the skip, but printing the instructions does
  // not. Otherwise it could loop infinitely as it traces a program to print.

  // Skip 0 (execute next instruction which just gives a constant).
  DoEvalTest({llvm::dwarf::DW_OP_skip, 0, 0, llvm::dwarf::DW_OP_lit9}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(9u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_skip(0), DW_OP_lit9");

  // Skip 1 (skip over user-defined instruction which would normally give an error).
  DoEvalTest({llvm::dwarf::DW_OP_skip, 1, 0, llvm::dwarf::DW_OP_hi_user, llvm::dwarf::DW_OP_lit9},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(9u),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_skip(1), INVALID_OPCODE(0xff), DW_OP_lit9");

  // Skip to the end should just terminate the program. The result when nothing is left on the stack
  // is 0.
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_skip, 1, 0, llvm::dwarf::DW_OP_nop}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_skip(1), DW_OP_nop");

  // Skip before the beginning is an error.
  DoEvalTest({llvm::dwarf::DW_OP_skip, 0, 0xff}, false, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(0), DwarfExprEval::ResultType::kPointer, "DW_OP_skip(-256)",
             "DWARF expression skips out-of-bounds.");
}

TEST_F(DwarfExprEvalTest, Bra) {
  // 0 @ top of stack means don't take the branch. This jumps out of bounds which should not be
  // taken.
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_bra, 0xff, 0, llvm::dwarf::DW_OP_lit9},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(9u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_bra(255), DW_OP_lit9");
  EXPECT_TRUE(eval().result_is_constant());

  // Nonzero means take the branch. This jumps over a user-defined instruction which would give an
  // error if executed.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_bra, 1, 0, llvm::dwarf::DW_OP_lo_user,
              llvm::dwarf::DW_OP_lit9},
             true, DwarfExprEval::Completion::kSync, DwarfStackEntry(9u),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_lit1, DW_OP_bra(1), DW_OP_GNU_push_tls_address, DW_OP_lit9");
}

TEST_F(DwarfExprEvalTest, Eq) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_eq}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit0, DW_OP_eq");
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_eq}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit1, DW_OP_eq");

  // Can't compare different types (this is a signed and a float value).
  auto uint_type = MakeUint32Type();
  auto float_type = MakeFloatType();
  eval().Clear();
  eval().Push(DwarfStackEntry(uint_type, uint128_t(17u)));
  eval().Push(DwarfStackEntry(float_type, 17.0f));
  DoEval(DwarfExpr({llvm::dwarf::DW_OP_eq}), false, DwarfExprEval::Completion::kSync,
         DwarfStackEntry(0u), DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Ge) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_ge}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit0, DW_OP_ge");
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_ge}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit1, DW_OP_ge");
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_ge}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_lit0, DW_OP_ge");

  // Can't compare different types (this is an unsigned int and an unsigned char which would be
  // comparable in C).
  auto uint_type = MakeUint32Type();
  auto uchar_type = MakeUnsignedChar8Type();
  eval().Clear();
  eval().Push(DwarfStackEntry(uint_type, uint128_t(17u)));
  eval().Push(DwarfStackEntry(uchar_type, uint128_t(17u)));
  DoEval(DwarfExpr({llvm::dwarf::DW_OP_ge}), false, DwarfExprEval::Completion::kSync,
         DwarfStackEntry(0u), DwarfExprEval::ResultType::kPointer);
}

TEST_F(DwarfExprEvalTest, Gt) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_gt}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit0, DW_OP_gt");
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_gt}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit1, DW_OP_gt");
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_gt}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_lit0, DW_OP_gt");
}

TEST_F(DwarfExprEvalTest, Le) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_le}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit0, DW_OP_le");
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_le}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit1, DW_OP_le");
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_le}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit1, DW_OP_lit0, DW_OP_le");
}

TEST_F(DwarfExprEvalTest, Lt) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lt}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit0, DW_OP_lt");
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lt}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit1, DW_OP_lt");
}

TEST_F(DwarfExprEvalTest, Ne) {
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_ne}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(0u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit0, DW_OP_ne");
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_ne}, true,
             DwarfExprEval::Completion::kSync, DwarfStackEntry(1u),
             DwarfExprEval::ResultType::kPointer, "DW_OP_lit0, DW_OP_lit1, DW_OP_ne");
}

TEST_F(DwarfExprEvalTest, Fbreg) {
  constexpr uint64_t kBase = 0x1000000;
  provider()->set_bp(kBase);

  DoEvalTest({llvm::dwarf::DW_OP_fbreg, 0}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(kBase), DwarfExprEval::ResultType::kPointer, "DW_OP_fbreg(0)");
  EXPECT_FALSE(eval().result_is_constant());

  // Note: 129 in SLEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_fbreg, 0x81, 0x01}, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(kBase + 129u), DwarfExprEval::ResultType::kPointer,
             "DW_OP_fbreg(129)");
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
  memcpy(mem.data(), &kMemoryContents, sizeof(kMemoryContents));
  provider()->AddMemory(kReg6 + kOffsetFromReg6, mem);

  DoEvalTest(program, true, DwarfExprEval::Completion::kAsync,
             DwarfStackEntry(kMemoryContents - 0x30), DwarfExprEval::ResultType::kPointer,
             "DW_OP_breg6(-40), DW_OP_deref, DW_OP_constu(48), DW_OP_minus");
  EXPECT_FALSE(eval().result_is_constant());
}

// Tests DW_OP_deref_size and DW_OP_deref_type.
TEST_F(DwarfExprEvalTest, DerefSizeAndType) {
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

  DoEvalTest(program, true, DwarfExprEval::Completion::kAsync,
             DwarfStackEntry(kMemValue + kAddAmount), DwarfExprEval::ResultType::kValue,
             "DW_OP_breg1(0), DW_OP_deref_size(1), DW_OP_plus_uconst(2), DW_OP_stack_value");
  EXPECT_FALSE(eval().result_is_constant());

  // Same thing but with DW_OP_deref_type pointing to a 1-byte char and adding a typed constant.
  constexpr uint8_t kDieOffset = 0x29;  // Offset from unit (<7 bits to avoid LEB encoding).
  // clang-format off
  const std::vector<uint8_t> program_type = {
      llvm::dwarf::DW_OP_breg1, 0,
      llvm::dwarf::DW_OP_deref_type, 0x01, kDieOffset,  // Deref one byte with the type.
      llvm::dwarf::DW_OP_const_type, kDieOffset, 1, kAddAmount,  // Push constant of char type.
      llvm::dwarf::DW_OP_plus,
      llvm::dwarf::DW_OP_stack_value};
  // clang-format on

  // Set up the type info for the character type.
  constexpr uint64_t kDieLoc = kUnitOffset + kDieOffset;
  auto char_type = MakeSignedChar8Type();
  symbol_factory().SetMockSymbol(kDieLoc, char_type);

  DoEvalTest(
      program_type, true, DwarfExprEval::Completion::kAsync,
      DwarfStackEntry(char_type, static_cast<DwarfStackEntry::SignedType>(kMemValue + kAddAmount)),
      DwarfExprEval::ResultType::kValue,
      "DW_OP_breg1(0), DW_OP_deref_type(size=1, die_offset=0x29), "
      "DW_OP_const_type(die_offset=0x29, data_size=1, data_bytes=0x02), DW_OP_plus, "
      "DW_OP_stack_value");
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
  DoEvalTest(program, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(kExpected),
             DwarfExprEval::ResultType::kValue, "DW_OP_implicit_value(16, 0x40009851eb851eb85000)");
  EXPECT_TRUE(eval().result_is_constant());

  // This program declares it has 0x10 bytes of data (2nd array value), but there are only 0x0f
  // values following it.
  std::vector<uint8_t> bad_program = {llvm::dwarf::DW_OP_implicit_value, 0x10,
                                      0x00, 0x50, 0xb8, 0x1e, 0x85, 0xeb, 0x51, 0x98,
                                      0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
  // clang-format on
  DoEvalTest(bad_program, false, DwarfExprEval::Completion::kSync, DwarfStackEntry(kExpected),
             DwarfExprEval::ResultType::kValue,
             "ERROR: \"Not enough data for DWARF implicit value.\"",
             "Not enough data for DWARF implicit value.");
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

  DoEvalTest(program, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kData,
             "DW_OP_breg3(0), DW_OP_lit1, DW_OP_shl, DW_OP_stack_value, DW_OP_piece(4), "
             "DW_OP_reg4, DW_OP_piece(4)");

  // Result should be {x = 2, y = 17}.
  EXPECT_EQ("02 00 00 00 11 00 00 00\n", eval().TakeResultData().ToString());
}

TEST_F(DwarfExprEvalTest, Piece_ValueUnknown) {
  // These expression were generated by GCC for this code with "-O1":
  //
  //   struct Foo {
  //     float f;
  //     char c;
  //     double d;
  //     uint64_t asdf = 32;
  //   };
  //
  //   Foo foo;
  //   foo.f = 78.0;
  //   foo.c = (char)argc;
  //
  // Both expressions show certain portions of the structure as being unknown with other values
  // being statically known, and some values being in registers.
  //
  // clang-format off
  std::vector<uint8_t> mostly_undefined{
    llvm::dwarf::DW_OP_piece, 0x10,    // 16 bytes undefined (f, c, d).
    llvm::dwarf::DW_OP_const1u, 0x20,  // Value of asdf = 32.
    llvm::dwarf::DW_OP_stack_value,
    llvm::dwarf::DW_OP_piece, 0x08
  };
  // clang-format on
  DoEvalTest(mostly_undefined, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kData,
             "DW_OP_piece(16), DW_OP_const1u(32), DW_OP_stack_value, DW_OP_piece(8)");

  EXPECT_EQ(
      "?? ?? ?? ?? ?? ?? ?? ??   ?? ?? ?? ?? ?? ?? ?? ??\n"  // 16 bytes undefined.
      "20 00 00 00 00 00 00 00\n",                           // uint64_t = 32.
      eval().TakeResultData().ToString());

  // This program defines a different implementation of the same struct where the float is defined.
  // clang-format off
  std::vector<uint8_t> partially_defined{
    llvm::dwarf::DW_OP_implicit_value, 0x04, 0x00, 0x00, 0x9c, 0x42,
    llvm::dwarf::DW_OP_piece, 0x04,    // 4 bytes undefined (the "float f").
    llvm::dwarf::DW_OP_reg3,           // rbx
    llvm::dwarf::DW_OP_piece, 0x01,    // Take the low byte of rbx for "char c".
    llvm::dwarf::DW_OP_piece, 0x0b,    // 11 bytes undefined (3 bytes padding, 8 bytes "double d").
    llvm::dwarf::DW_OP_const1u, 0x20,  // Value of asdf = 32.
    llvm::dwarf::DW_OP_stack_value,
    llvm::dwarf::DW_OP_piece, 0x08
  };
  // clang-format on

  provider()->AddRegisterValue(kDWARFReg3ID, true, 0x8877665544332211u);  // rbx value.
  DoEvalTest(partially_defined, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kData,
             "DW_OP_implicit_value(4, 0x429c0000), DW_OP_piece(4), DW_OP_reg3, DW_OP_piece(1), "
             "DW_OP_piece(11), DW_OP_const1u(32), DW_OP_stack_value, DW_OP_piece(8)");
  EXPECT_EQ(
      //           Low byte of rbx
      //           |
      // Float---- |  Pad-----   Double-----------------
      "00 00 9c 42 11 ?? ?? ??   ?? ?? ?? ?? ?? ?? ?? ??\n"

      // uint64---------------
      "20 00 00 00 00 00 00 00\n",
      eval().TakeResultData().ToString());

  // A complex program using "entry_value" that Clang produced (more general version of above).
  // clang-format off
  std::vector<uint8_t> entry_value{
    llvm::dwarf::DW_OP_implicit_value, 0x4, 0x00, 0x00, 0x9c, 0x42,
    llvm::dwarf::DW_OP_piece, 0x4,
    llvm::dwarf::DW_OP_GNU_entry_value, 0x1,  // 1 byte "entry value" expression follows.
    llvm::dwarf::DW_OP_reg5,                  // The actual "entry value" expression.
    llvm::dwarf::DW_OP_stack_value,
    llvm::dwarf::DW_OP_piece, 0x1,
    llvm::dwarf::DW_OP_piece, 0xb,
    llvm::dwarf::DW_OP_const1u, 0x20,
    llvm::dwarf::DW_OP_stack_value,
    llvm::dwarf::DW_OP_piece, 0x8
  };
  // clang-format on

  // Provide the entry value for register 5.
  auto entry_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  provider()->set_entry_provider(entry_provider);
  entry_provider->AddRegisterValue(kDWARFReg5ID, true, 0x8877665544332211);

  DoEvalTest(entry_value, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kData,
             "DW_OP_implicit_value(4, 0x429c0000), DW_OP_piece(4), "
             "DW_OP_GNU_entry_value(DW_OP_reg5), DW_OP_stack_value, DW_OP_piece(1), "
             "DW_OP_piece(11), DW_OP_const1u(32), DW_OP_stack_value, DW_OP_piece(8)");
  EXPECT_EQ(
      //           Low byte of entry value reg5.
      //           |
      // Float---- |  Pad-----   Double-----------------
      "00 00 9c 42 11 ?? ?? ??   ?? ?? ?? ?? ?? ?? ?? ??\n"

      // uint64---------------
      "20 00 00 00 00 00 00 00\n",
      eval().TakeResultData().ToString());
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

  DoEvalTest(program, true, DwarfExprEval::Completion::kAsync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kData,
             "DW_OP_breg3(0), DW_OP_lit1, DW_OP_shl, DW_OP_stack_value, DW_OP_piece(4), "
             "DW_OP_breg4(0), DW_OP_piece(4)");

  // Result should be {x = 2, y = 17}.
  EXPECT_EQ("02 00 00 00 11 00 00 00\n", eval().TakeResultData().ToString());
}

TEST_F(DwarfExprEvalTest, GetTLSAddr) {
  std::vector<uint8_t> program{
      llvm::dwarf::DW_OP_const8u, 0, 1, 2, 3, 4, 5, 6, 7, llvm::dwarf::DW_OP_form_tls_address,
  };

  provider()->set_tls_segment(0xdeadbeef);

  DoEvalTest(program, true, DwarfExprEval::Completion::kAsync, DwarfStackEntry(0x7060504e1afbfef),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_const8u(0x706050403020100), DW_OP_form_tls_address");
}

// Tests the pretty formatting mode that decodes registers and simplfies literals.
TEST_F(DwarfExprEvalTest, PrettyPrint) {
  eval().Clear();
  std::string stringified =
      eval().ToString(DwarfExpr({llvm::dwarf::DW_OP_reg3, llvm::dwarf::DW_OP_breg0, 2,
                                 llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_plus_uconst, 1,
                                 // This address is "1" relative to the module base.
                                 llvm::dwarf::DW_OP_addr, 1, 0, 0, 0, 0, 0, 0, 0}),
                      true);
  EXPECT_EQ(
      "register(x3), register(x0) + 2, push(3), + 1, push(" + to_hex_string(kModuleBase + 1) + ")",
      stringified);
}

TEST_F(DwarfExprEvalTest, EntryValue) {
  auto entry_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  provider()->set_entry_provider(entry_provider);

  constexpr uint64_t kEntryX0 = 0x12783645190;
  entry_provider->AddRegisterValue(kDWARFReg0ID, true, kEntryX0);

  // The most common type of "entry value" expression is just the register value directly.
  std::vector<uint8_t> simple_program{
      llvm::dwarf::DW_OP_entry_value,
      1,
      llvm::dwarf::DW_OP_reg0,
      llvm::dwarf::DW_OP_stack_value,
  };
  DoEvalTest(simple_program, true, DwarfExprEval::Completion::kSync, DwarfStackEntry(kEntryX0),
             DwarfExprEval::ResultType::kValue, "DW_OP_entry_value(DW_OP_reg0), DW_OP_stack_value");
  eval().Clear();

  // An entry value expression with a bad length.
  std::vector<uint8_t> bad_length{llvm::dwarf::DW_OP_entry_value, 23, llvm::dwarf::DW_OP_reg0};
  DoEvalTest(bad_length, false, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kValue,
             "ERROR: \"DW_OP_entry_value sub expression is a bad length.\"",
             "DW_OP_entry_value sub expression is a bad length.");
  eval().Clear();

  // Asynchronous entry-value evaluation. In practice this will seldon happen, but it probably means
  // the entry value is computable from the stack in the calling function.
  constexpr uint8_t kEntryOffset = 0x31;  // Register offset in entry frame.
  constexpr uint8_t kTopOffset = 0x01;    // Register offset in top frame.
  std::vector<uint8_t> complex_program{
      llvm::dwarf::DW_OP_entry_value, 3,  // 3 bytes in the program below.

      // Entry value program.
      llvm::dwarf::DW_OP_breg6, kEntryOffset, llvm::dwarf::DW_OP_deref,

      // This is evaluated in the top frame so will get a different value for reg6.
      llvm::dwarf::DW_OP_breg6, kTopOffset, llvm::dwarf::DW_OP_minus};

  // Register values in both frames.
  constexpr uint64_t kEntryX6 = 0x12345678;
  entry_provider->AddRegisterValue(kDWARFReg6ID, true, kEntryX6);
  constexpr uint64_t kTopX6 = 0x99;
  provider()->AddRegisterValue(kDWARFReg6ID, true, kTopX6);

  // The entry frame expression computes *(X6 + kEntryOffset).
  constexpr uint64_t kEntryAddress = kEntryX6 + kEntryOffset;
  constexpr uint64_t kEntryValue = 0x1122334455667788;
  std::vector<uint8_t> entry_value = {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
  entry_provider->AddMemory(kEntryAddress, entry_value);

  // The outer expression computs (X6 + offset) and then subtracts that from the entry value.
  constexpr uint64_t kExpected = kEntryValue - (kTopX6 + kTopOffset);

  DoEvalTest(complex_program, true, DwarfExprEval::Completion::kAsync, DwarfStackEntry(kExpected),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_entry_value(DW_OP_breg6(49), DW_OP_deref), DW_OP_breg6(1), DW_OP_minus");
  eval().Clear();

  // Sometimes the expressions are printed from debug commands where there is no entry data
  // provider. Printing should still work in this case but evaluation should fail.
  provider()->set_entry_provider(nullptr);
  DoEvalTest(complex_program, false, DwarfExprEval::Completion::kSync, DwarfStackEntry(kExpected),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_entry_value(DW_OP_breg6(49), DW_OP_deref), DW_OP_breg6(1), DW_OP_minus");
}

TEST_F(DwarfExprEvalTest, ConstType) {
  constexpr uint8_t kDieOffset = 0x29;  // Offset from unit (<7 bits to avoid LEB encoding).
  constexpr uint64_t kDieLoc = kUnitOffset + kDieOffset;

  auto uint32_type = MakeUint32Type();
  symbol_factory().SetMockSymbol(kDieLoc, uint32_type);

  // Normal typed expression.
  std::vector<uint8_t> expr1{llvm::dwarf::DW_OP_const_type,
                             kDieOffset,  // 1st param: ULEB unit-relative DIE offset.
                             4,           // 2nd param: (1 byte) data size.
                             0x22,
                             0x33,
                             0x44,
                             0x55,  // 3rd param: data.
                             llvm::dwarf::DW_OP_stack_value};
  DoEvalTest(expr1, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(uint32_type, DwarfStackEntry::UnsignedType(0x55443322u)),
             DwarfExprEval::ResultType::kValue,
             "DW_OP_const_type(die_offset=0x29, data_size=4, data_bytes=0x22 0x33 0x44 0x55), "
             "DW_OP_stack_value");

  // Invalid DIE offset.
  std::vector<uint8_t> expr2{llvm::dwarf::DW_OP_const_type,
                             2,  // 1st param: ULEB unit-relative DIE offset.
                             4,  // 2nd param: (1 byte) data size.
                             0x22,
                             0x33,
                             0x44,
                             0x55};  // 3rd param: data.
  DoEvalTest(expr2, false, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_const_type(die_offset=0x2, data_size=4, data_bytes=0x22 0x33 0x44 0x55)");

  // 0 data bytes.
  std::vector<uint8_t> expr3{llvm::dwarf::DW_OP_const_type,
                             kDieOffset,  // 1st param: ULEB unit-relative DIE offset.
                             0,           // 2nd param: (1 byte) data size.
                                          // 3rd param: no data.
                             llvm::dwarf::DW_OP_stack_value};
  DoEvalTest(expr3, false, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_const_type(die_offset=0x29, data_size=0, data_bytes=), DW_OP_stack_value");

  // Too many data bytes.
  // clang-format off
  std::vector<uint8_t> expr4{llvm::dwarf::DW_OP_const_type,
                             kDieOffset,  // 1st param: ULEB unit-relative DIE offset.
                             20,          // 2nd param: (1 byte) data size.
                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             llvm::dwarf::DW_OP_stack_value};
  // clang-format on
  DoEvalTest(
      expr4, false, DwarfExprEval::Completion::kSync, DwarfStackEntry(0),
      DwarfExprEval::ResultType::kPointer,
      "DW_OP_const_type(die_offset=0x29, data_size=20, data_bytes=0x00 0x00 0x00 0x00 0x00 0x00 "
      "0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00), DW_OP_stack_value");
}

TEST_F(DwarfExprEvalTest, RegvalType) {
  constexpr uint8_t kIntDieOffset = 0x29;  // Offset from unit (<7 bits to avoid LEB encoding).
  constexpr uint64_t kIntDieLoc = kUnitOffset + kIntDieOffset;
  constexpr uint8_t kFloatDieOffset = 0x37;
  constexpr uint64_t kFloatDieLoc = kUnitOffset + kFloatDieOffset;

  auto int64_type = MakeInt64Type();
  symbol_factory().SetMockSymbol(kIntDieLoc, int64_type);
  auto float_type = MakeFloatType();
  symbol_factory().SetMockSymbol(kFloatDieLoc, float_type);

  provider()->AddRegisterValue(kDWARFReg0ID, true, 100);

  float kFloatVal = 3.14159f;
  uint64_t float_as_register = 0;
  memcpy(&float_as_register, &kFloatVal, sizeof(float));
  provider()->AddRegisterValue(kDWARFReg9ID, false, float_as_register);

  // Reg0, synchronous int64 (=100).
  DoEvalTest({llvm::dwarf::DW_OP_regval_type, 0, kIntDieOffset}, true,
             DwarfExprEval::Completion::kSync,
             DwarfStackEntry(int64_type, DwarfStackEntry::SignedType(100)),
             DwarfExprEval::ResultType::kPointer, "DW_OP_regval_type(reg=0, die_offset=0x29)");
  EXPECT_EQ(RegisterID::kARMv8_x0, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());

  // Reg9, asynchronous float. This is stored in the low bits of the register.
  DoEvalTest({llvm::dwarf::DW_OP_regval_type, 9, kFloatDieOffset, llvm::dwarf::DW_OP_stack_value},
             true, DwarfExprEval::Completion::kAsync, DwarfStackEntry(float_type, kFloatVal),
             DwarfExprEval::ResultType::kValue,
             "DW_OP_regval_type(reg=9, die_offset=0x37), DW_OP_stack_value");
  EXPECT_EQ(RegisterID::kARMv8_x9, eval().current_register_id());
  EXPECT_FALSE(eval().result_is_constant());
}

// Tests DW_OP_convert and DW_OP_reinterpret.
TEST_F(DwarfExprEvalTest, Casts) {
  // Die offsets all uses <= 7 bits so we don't have to use multibyte LEB encoding.
  constexpr uint8_t kInt64DieOffset = 0x29;
  constexpr uint64_t kInt64DieLoc = kUnitOffset + kInt64DieOffset;
  auto int64_type = MakeInt64Type();
  symbol_factory().SetMockSymbol(kInt64DieLoc, int64_type);

  constexpr uint8_t kUint32DieOffset = 0x31;
  constexpr uint64_t kUint32DieLoc = kUnitOffset + kUint32DieOffset;
  auto uint32_type = MakeUint32Type();
  symbol_factory().SetMockSymbol(kUint32DieLoc, uint32_type);

  constexpr uint8_t kFloatDieOffset = 0x33;
  constexpr uint64_t kFloatDieLoc = kUnitOffset + kFloatDieOffset;
  auto float_type = MakeFloatType();
  symbol_factory().SetMockSymbol(kFloatDieLoc, float_type);

  constexpr uint8_t kDoubleDieOffset = 0x35;
  constexpr uint64_t kDoubleDieLoc = kUnitOffset + kDoubleDieOffset;
  auto double_type = MakeDoubleType();
  symbol_factory().SetMockSymbol(kDoubleDieLoc, double_type);

  // Program fragment that adds a typed double to the stack.
  constexpr double kDoubleSource = 3.1415926535;
  uint8_t double_arr[sizeof(double)];
  memcpy(double_arr, &kDoubleSource, sizeof(double));
  // clang-format off
  std::vector<uint8_t> double_const_source = {
      llvm::dwarf::DW_OP_const_type, kDoubleDieOffset, static_cast<uint8_t>(sizeof(double)),
      double_arr[0], double_arr[1], double_arr[2], double_arr[3],
      double_arr[4], double_arr[5], double_arr[6], double_arr[7]};
  // clang-format on

  // Static cast the double to a uint32 = 3;
  std::vector<uint8_t> static_double_to_uint = double_const_source;
  static_double_to_uint.push_back(llvm::dwarf::DW_OP_convert);
  static_double_to_uint.push_back(kUint32DieOffset);
  DoEvalTest(static_double_to_uint, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(uint32_type, DwarfStackEntry::UnsignedType(3)),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_const_type(die_offset=0x35, data_size=8, data_bytes=0x44 0x17 0x41 0x54 0xfb "
             "0x21 0x09 0x40), DW_OP_convert(die_offset=0x31)");

  // Static cast the double to a float.
  std::vector<uint8_t> static_double_to_float = double_const_source;
  static_double_to_float.push_back(llvm::dwarf::DW_OP_convert);
  static_double_to_float.push_back(kFloatDieOffset);
  DoEvalTest(static_double_to_float, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(float_type, static_cast<float>(kDoubleSource)),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_const_type(die_offset=0x35, data_size=8, data_bytes=0x44 0x17 0x41 0x54 0xfb "
             "0x21 0x09 0x40), DW_OP_convert(die_offset=0x33)");

  // Reinterpret the double to an int64.
  std::vector<uint8_t> reint_double_to_int64 = double_const_source;
  reint_double_to_int64.push_back(llvm::dwarf::DW_OP_reinterpret);
  reint_double_to_int64.push_back(kInt64DieOffset);

  int64_t expected_int64 = 0;
  memcpy(&expected_int64, &kDoubleSource, sizeof(int64_t));
  DoEvalTest(reint_double_to_int64, true, DwarfExprEval::Completion::kSync,
             DwarfStackEntry(int64_type, DwarfStackEntry::SignedType(expected_int64)),
             DwarfExprEval::ResultType::kPointer,
             "DW_OP_const_type(die_offset=0x35, data_size=8, data_bytes=0x44 0x17 0x41 0x54 0xfb "
             "0x21 0x09 0x40), DW_OP_reinterpret(die_offset=0x29)");

  // This is a real program from Clang. It expresses a boolean passed in a register
  // DW_OP_breg5 RDI+0, DW_OP_constu 0xffffffff, DW_OP_and, DW_OP_convert (0x0000002f)
  // "DW_ATE_unsigned_1", DW_OP_convert (0x00000034) "DW_ATE_unsigned_8", DW_OP_stack_value
}

}  // namespace zxdb
