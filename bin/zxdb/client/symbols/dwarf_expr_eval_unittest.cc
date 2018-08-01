// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_expr_eval.h"
#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "llvm/BinaryFormat/Dwarf.h"

namespace zxdb {

namespace {

class MockDataProvider : public SymbolDataProvider {
 public:
  MockDataProvider();

  // Adds the given canned result for the given register. Set synchronous if
  // the register contents should be synchronously available, false if it
  // should require a callback to retrieve.
  void AddRegisterValue(int register_num, bool synchronous, uint64_t value);

  // SymbolDataProvider implementation.
  bool GetRegister(int dwarf_register_number, uint64_t* output) override;
  void GetRegisterAsync(
      int dwarf_register_number,
      std::function<void(bool success, uint64_t value)> callback) override;
  void GetMemoryAsync(
      uint64_t address, uint32_t size,
      std::function<void(const uint8_t* data)> callback) override;

 private:
  struct RegData {
    RegData() = default;
    RegData(bool sync, uint64_t v) : synchronous(sync), value(v) {}

    bool synchronous = false;
    uint64_t value = 0;
  };

  std::map<int, RegData> regs_;

  fxl::WeakPtrFactory<MockDataProvider> weak_factory_;
};

MockDataProvider::MockDataProvider() : weak_factory_(this) {}

void MockDataProvider::AddRegisterValue(int register_num, bool synchronous,
                                        uint64_t value) {
  regs_[register_num] = RegData(synchronous, value);
}

bool MockDataProvider::GetRegister(int dwarf_register_number,
                                   uint64_t* output) {
  const auto& found = regs_.find(dwarf_register_number);
  if (found == regs_.end())
    return false;

  if (!found->second.synchronous)
    return false;  // Force synchronous query.

  *output = found->second.value;
  return true;
}

void MockDataProvider::GetRegisterAsync(
    int dwarf_register_number,
    std::function<void(bool success, uint64_t value)> callback) {
  debug_ipc::MessageLoop::Current()->PostTask([
    callback, weak_provider = weak_factory_.GetWeakPtr(), dwarf_register_number
  ]() {
    if (!weak_provider) {
      ADD_FAILURE();  // Destroyed before callback ready.
      return;
    }

    const auto& found = weak_provider->regs_.find(dwarf_register_number);
    if (found == weak_provider->regs_.end())
      callback(false, 0);
    callback(true, found->second.value);
  });
}

void MockDataProvider::GetMemoryAsync(
    uint64_t address, uint32_t size,
    std::function<void(const uint8_t* data)> callback) {
  // FIXME(brettw) implement this.
}

class DwarfExprEvalTest : public testing::Test {
 public:
  DwarfExprEvalTest() { loop_.Init(); }
  ~DwarfExprEvalTest() { loop_.Cleanup(); }

  DwarfExprEval& eval() { return eval_; }
  MockDataProvider& provider() { return provider_; }
  debug_ipc::MessageLoop& loop() { return loop_; }

  // If expected_message is non-null, this error message will be expected on
  // failure. The expected result will only be checked on success, true, and
  // the expected_message will only be checked on failure.
  void DoEvalTest(const std::vector<uint8_t> data, bool expected_success,
                  DwarfExprEval::Completion expected_completion,
                  uint64_t expected_result,
                  const char* expected_message = nullptr);

 private:
  DwarfExprEval eval_;
  debug_ipc::PlatformMessageLoop loop_;
  MockDataProvider provider_;
};

void DwarfExprEvalTest::DoEvalTest(
    const std::vector<uint8_t> data, bool expected_success,
    DwarfExprEval::Completion expected_completion, uint64_t expected_result,
    const char* expected_message) {
  bool callback_issued = false;
  EXPECT_EQ(expected_completion,
            eval_.Eval(&provider_, data, [&callback_issued, expected_success,
                                          expected_completion, expected_result,
                                          expected_message](DwarfExprEval* eval,
                                                            const Err& err) {
              EXPECT_TRUE(eval->is_complete());
              EXPECT_EQ(expected_success, !err.has_error()) << err.msg();
              if (err.ok())
                EXPECT_EQ(expected_result, eval->GetResult());
              else if (expected_message)
                EXPECT_EQ(expected_message, err.msg());
              callback_issued = true;

              // When we're doing an async completion, need to exit the message
              // loop to continue with the test.
              if (expected_completion == DwarfExprEval::Completion::kAsync)
                debug_ipc::MessageLoop::Current()->QuitNow();
            }));

  if (expected_completion == DwarfExprEval::Completion::kAsync) {
    // In the async case the message loop needs to be run to get the result.
    EXPECT_FALSE(eval_.is_complete());
    EXPECT_FALSE(callback_issued);

    // Ensure the callback was made after running the loop.
    loop_.Run();
  }

  EXPECT_TRUE(eval_.is_complete());
  EXPECT_TRUE(callback_issued);
}

}  // namespace

TEST_F(DwarfExprEvalTest, NoResult) {
  const char kNoResults[] = "DWARF expression produced no results.";

  // Empty expression.
  DoEvalTest({}, false, DwarfExprEval::Completion::kSync, 0, kNoResults);

  // Nonempty expression that produces no results.
  DoEvalTest({llvm::dwarf::DW_OP_nop}, false, DwarfExprEval::Completion::kSync,
             0, kNoResults);
}

// Tests synchronously reading a single register.
TEST_F(DwarfExprEvalTest, SyncRegister) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider().AddRegisterValue(0, true, kValue);

  DoEvalTest({llvm::dwarf::DW_OP_reg0}, true, DwarfExprEval::Completion::kSync,
             kValue);
}

// Tests the encoding form of registers as parameters to an operation rather
// than the version encoded in the operation.
//
// Also tests DW_OP_nop.
TEST_F(DwarfExprEvalTest, SyncRegisterAsNumber) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider().AddRegisterValue(1, true, kValue);

  // Use "regx" which will read the register number as a ULEB following it.
  // The byte is the ULEB-encoded version of 1 (high bit set to indicate it's
  // the last byte).
  std::vector<uint8_t> expr_data;
  expr_data.push_back(llvm::dwarf::DW_OP_nop);
  expr_data.push_back(llvm::dwarf::DW_OP_regx);
  expr_data.push_back(0b10000001);

  DoEvalTest(expr_data, true, DwarfExprEval::Completion::kSync, kValue);
}

// Tests asynchronously reading a single register.
TEST_F(DwarfExprEvalTest, AsyncRegister) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider().AddRegisterValue(0, false, kValue);

  DoEvalTest({llvm::dwarf::DW_OP_reg0}, true, DwarfExprEval::Completion::kAsync,
             kValue);
}

// Tests synchronously hitting an invalid opcode.
TEST_F(DwarfExprEvalTest, SyncInvalidOp) {
  // Make a program that consists only of a user-defined opcode (not supported).
  DoEvalTest({llvm::dwarf::DW_OP_lo_user}, false,
             DwarfExprEval::Completion::kSync, 0,
             "Invalid opcode 0xe0 in DWARF expression.");
}

// Tests synchronously hitting an invalid opcode (async error handling).
TEST_F(DwarfExprEvalTest, AsyncInvalidOp) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider().AddRegisterValue(0, false, kValue);

  // Make a program that consists of getting an async register and then
  // executing an invalid opcode.
  std::vector<uint8_t> expr_data;
  expr_data.push_back(llvm::dwarf::DW_OP_reg0);
  expr_data.push_back(llvm::dwarf::DW_OP_lo_user + 1);

  DoEvalTest(expr_data, false, DwarfExprEval::Completion::kAsync, 0,
             "Invalid opcode 0xe1 in DWARF expression.");
}

// Tests the special opcodes that also encode a 0-31 literal.
TEST_F(DwarfExprEvalTest, LiteralOp) {
  DoEvalTest({llvm::dwarf::DW_OP_lit4}, true, DwarfExprEval::Completion::kSync,
             4u);
}

TEST_F(DwarfExprEvalTest, Addr) {
  // Always expect 8-byte (64-bit) addresses.
  DoEvalTest(
      {llvm::dwarf::DW_OP_addr, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0xf0},
      true, DwarfExprEval::Completion::kSync, 0xf001020304050607u);
}

// Tests that reading fixed-length constant without enough room fails.
TEST_F(DwarfExprEvalTest, Const4ReadOffEnd) {
  DoEvalTest({llvm::dwarf::DW_OP_const4u, 0xf0}, false,
             DwarfExprEval::Completion::kSync, 0,
             "Bad number format in DWARF expression.");
}

// Tests that reading a ULEB number without enough room fails.
TEST_F(DwarfExprEvalTest, ConstReadOffEnd) {
  // Note that LLVM allows LEB numbers to run off the end, and in that case
  // just stops reading data and reports the bits read.
  DoEvalTest({llvm::dwarf::DW_OP_constu}, false,
             DwarfExprEval::Completion::kSync, 0,
             "Bad number format in DWARF expression.");
}

TEST_F(DwarfExprEvalTest, Breg) {
  provider().AddRegisterValue(0, true, 100);
  provider().AddRegisterValue(9, false, 200);

  // reg0 (=100) + 129 = 229 (synchronous).
  // Note: 129 in SLEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_breg0, 0x81, 0x01}, true,
             DwarfExprEval::Completion::kSync, 229u);

  // reg9 (=200) - 127 = 73 (asynchronous).
  // -127 in SLEB is 0x81, 0x7f (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_breg9, 0x81, 0x7f}, true,
             DwarfExprEval::Completion::kAsync, 73u);
}

TEST_F(DwarfExprEvalTest, Bregx) {
  provider().AddRegisterValue(0, true, 100);
  provider().AddRegisterValue(9, false, 200);

  // reg0 (=100) + 129 = 229 (synchronous).
  // Note: 129 in SLEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_bregx, 0x00, 0x81, 0x01}, true,
             DwarfExprEval::Completion::kSync, 229u);

  // reg9 (=200) - 127 = 73 (asynchronous).
  // -127 in SLEB is 0x81, 0x7f (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_bregx, 0x09, 0x81, 0x7f}, true,
             DwarfExprEval::Completion::kAsync, 73u);
}

TEST_F(DwarfExprEvalTest, Const1s) {
  DoEvalTest({llvm::dwarf::DW_OP_const1s, static_cast<uint8_t>(-3)}, true,
             DwarfExprEval::Completion::kSync, static_cast<uint64_t>(-3));
}

TEST_F(DwarfExprEvalTest, Const1u) {
  DoEvalTest({llvm::dwarf::DW_OP_const1u, 0xf0}, true,
             DwarfExprEval::Completion::kSync, 0xf0);
}

TEST_F(DwarfExprEvalTest, Const2s) {
  DoEvalTest({llvm::dwarf::DW_OP_const2s, static_cast<uint8_t>(-3), 0xff}, true,
             DwarfExprEval::Completion::kSync, static_cast<uint64_t>(-3));
}

TEST_F(DwarfExprEvalTest, Const2u) {
  DoEvalTest({llvm::dwarf::DW_OP_const2u, 0x01, 0xf0}, true,
             DwarfExprEval::Completion::kSync, 0xf001);
}

TEST_F(DwarfExprEvalTest, Const4s) {
  DoEvalTest(
      {llvm::dwarf::DW_OP_const4s, static_cast<uint8_t>(-3), 0xff, 0xff, 0xff},
      true, DwarfExprEval::Completion::kSync, static_cast<uint64_t>(-3));
}

TEST_F(DwarfExprEvalTest, Const4u) {
  DoEvalTest({llvm::dwarf::DW_OP_const4u, 0x03, 0x02, 0x01, 0xf0}, true,
             DwarfExprEval::Completion::kSync, 0xf0010203);
}

TEST_F(DwarfExprEvalTest, Const8s) {
  DoEvalTest({llvm::dwarf::DW_OP_const8s, static_cast<uint8_t>(-3), 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff},
             true, DwarfExprEval::Completion::kSync, static_cast<uint64_t>(-3));
}

TEST_F(DwarfExprEvalTest, Const8u) {
  DoEvalTest({llvm::dwarf::DW_OP_const8u, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02,
              0x01, 0xf0},
             true, DwarfExprEval::Completion::kSync, 0xf001020304050607u);
}

TEST_F(DwarfExprEvalTest, Consts) {
  // -127 in SLEB is 0x81, 0x7f (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_consts, 0x81, 0x7f}, true,
             DwarfExprEval::Completion::kSync, static_cast<uint64_t>(-127));
}

// Tests both "constu" and "drop".
TEST_F(DwarfExprEvalTest, ConstuDrop) {
  // 129 in ULEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_constu, 0x81, 0x01, llvm::dwarf::DW_OP_lit0,
              llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, 129u);
}

// Tests both "dup" and "add".
TEST_F(DwarfExprEvalTest, DupAdd) {
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_dup,
              llvm::dwarf::DW_OP_plus},
             true, DwarfExprEval::Completion::kSync, 16u);
}

TEST_F(DwarfExprEvalTest, Neg) {
  // Negate one should give -1 casted to unsigned.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_neg}, true,
             DwarfExprEval::Completion::kSync, 0xffffffffffffffffu);

  // Double negate should come back to 1.
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_neg, llvm::dwarf::DW_OP_neg},
      true, DwarfExprEval::Completion::kSync, 1u);
}

TEST_F(DwarfExprEvalTest, Not) {
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_not}, true,
             DwarfExprEval::Completion::kSync, 0xfffffffffffffffeu);
}

TEST_F(DwarfExprEvalTest, Or) {
  // 8 | 1 = 9.
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_or},
      true, DwarfExprEval::Completion::kSync, 9u);
}

TEST_F(DwarfExprEvalTest, Mul) {
  // 8 * 9 = 72.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit9,
              llvm::dwarf::DW_OP_mul},
             true, DwarfExprEval::Completion::kSync, 72u);
}

TEST_F(DwarfExprEvalTest, Minus) {
  // 8 - 2 = 6.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_minus},
             true, DwarfExprEval::Completion::kSync, 6u);
}

TEST_F(DwarfExprEvalTest, Over) {
  // Stack of (1, 2), this pushes "1" on the top.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_over},
             true, DwarfExprEval::Completion::kSync, 1u);

  // Same operation with a drop to check the next-to-top item.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_over, llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, 2u);
}

TEST_F(DwarfExprEvalTest, Pick) {
  // Stack of 1, 2, 3. Pick 0 -> 3.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_pick, 0},
             true, DwarfExprEval::Completion::kSync, 3u);

  // Stack of 1, 2, 3. Pick 2 -> 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_pick, 2},
             true, DwarfExprEval::Completion::kSync, 1u);

  // Stack of 1, 2, 3. Pick 3 -> error.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_pick, 3},
             false, DwarfExprEval::Completion::kSync, 0u,
             "Stack underflow for DWARF expression.");
}

TEST_F(DwarfExprEvalTest, Swap) {
  // 1, 2, swap -> 2, 1
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_swap},
             true, DwarfExprEval::Completion::kSync, 1u);
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_swap, llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, 2u);
}

TEST_F(DwarfExprEvalTest, Rot) {
  // 1, 2, 3, rot -> 3, 1, 2 (test with 0, 1, and 2 "drops" to check all 3
  // stack elements).
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_rot},
             true, DwarfExprEval::Completion::kSync, 2u);
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_rot,
              llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, 1u);
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_rot,
              llvm::dwarf::DW_OP_drop, llvm::dwarf::DW_OP_drop},
             true, DwarfExprEval::Completion::kSync, 3u);
}

TEST_F(DwarfExprEvalTest, Abs) {
  // Abs of 1 -> 1.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_abs}, true,
             DwarfExprEval::Completion::kSync, 1u);

  // Abs of -1 -> 1.
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_neg, llvm::dwarf::DW_OP_abs},
      true, DwarfExprEval::Completion::kSync, 1u);
}

TEST_F(DwarfExprEvalTest, And) {
  // 3 (=0b11) & 5 (=0b101) = 1
  DoEvalTest({llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_lit5,
              llvm::dwarf::DW_OP_and},
             true, DwarfExprEval::Completion::kSync, 1u);
}

TEST_F(DwarfExprEvalTest, Div) {
  // 8 / -2 = -4.
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_neg, llvm::dwarf::DW_OP_div},
             true, DwarfExprEval::Completion::kSync, static_cast<uint64_t>(-4));
}

TEST_F(DwarfExprEvalTest, Mod) {
  // 7 % 2 = 1
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_lit2,
              llvm::dwarf::DW_OP_mod},
             true, DwarfExprEval::Completion::kSync, 1);
}

TEST_F(DwarfExprEvalTest, PlusUconst) {
  // 7 + 129 = 136. 129 in ULEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_plus_uconst, 0x81, 0x01},
      true, DwarfExprEval::Completion::kSync, 136u);
}

TEST_F(DwarfExprEvalTest, Shr) {
  // 8 >> 1 = 4
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit1,
              llvm::dwarf::DW_OP_shr},
             true, DwarfExprEval::Completion::kSync, 4u);
}

TEST_F(DwarfExprEvalTest, Shra) {
  // -7 (=0b1111...1111001) >> 2 = -2 (=0b1111...1110)
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_neg,
              llvm::dwarf::DW_OP_lit2, llvm::dwarf::DW_OP_shra},
             true, DwarfExprEval::Completion::kSync, static_cast<uint64_t>(-2));
}

TEST_F(DwarfExprEvalTest, Shl) {
  // 8 << 1 = 16
  DoEvalTest({llvm::dwarf::DW_OP_lit8, llvm::dwarf::DW_OP_lit1,
              llvm::dwarf::DW_OP_shl},
             true, DwarfExprEval::Completion::kSync, 16u);
}

TEST_F(DwarfExprEvalTest, Xor) {
  // 7 (=0b111) ^ 9 (=0b1001) = 14 (=0b1110)
  DoEvalTest({llvm::dwarf::DW_OP_lit7, llvm::dwarf::DW_OP_lit9,
              llvm::dwarf::DW_OP_xor},
             true, DwarfExprEval::Completion::kSync, 14u);
}

TEST_F(DwarfExprEvalTest, Skip) {
  // Skip 0 (execute next instruction which just gives a constant).
  DoEvalTest({llvm::dwarf::DW_OP_skip, 0, 0, llvm::dwarf::DW_OP_lit9}, true,
             DwarfExprEval::Completion::kSync, 9u);

  // Skip 1 (skip over user-defined instruction which would normally give an
  // error).
  DoEvalTest({llvm::dwarf::DW_OP_skip, 1, 0, llvm::dwarf::DW_OP_lo_user,
              llvm::dwarf::DW_OP_lit9},
             true, DwarfExprEval::Completion::kSync, 9u);

  // Skip to the end should just terminate the program. The result when nothing
  // is left on the stack is 0.
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_skip, 1, 0,
              llvm::dwarf::DW_OP_nop},
             true, DwarfExprEval::Completion::kSync, 0);

  // Skip before the beginning is an error.
  DoEvalTest({llvm::dwarf::DW_OP_skip, 0, 0xff}, false,
             DwarfExprEval::Completion::kSync, 0,
             "DWARF expression skips out-of-bounds.");
}

TEST_F(DwarfExprEvalTest, Bra) {
  // 0 @ top of stack means don't take the branch. This jumps out of bounds
  // which should not be taken.
  DoEvalTest({llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_bra, 0xff, 0,
              llvm::dwarf::DW_OP_lit9},
             true, DwarfExprEval::Completion::kSync, 9u);

  // Nonzero means take the branch. This jumps over a user-defined instruction
  // which would give an error if executed.
  DoEvalTest({llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_bra, 1, 0,
              llvm::dwarf::DW_OP_lo_user, llvm::dwarf::DW_OP_lit9},
             true, DwarfExprEval::Completion::kSync, 9u);
}

TEST_F(DwarfExprEvalTest, Eq) {
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_eq},
      true, DwarfExprEval::Completion::kSync, 1u);
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_eq},
      true, DwarfExprEval::Completion::kSync, 0u);
}

TEST_F(DwarfExprEvalTest, Ge) {
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_ge},
      true, DwarfExprEval::Completion::kSync, 1u);
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_ge},
      true, DwarfExprEval::Completion::kSync, 0u);
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_ge},
      true, DwarfExprEval::Completion::kSync, 1u);
}

TEST_F(DwarfExprEvalTest, Gt) {
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_gt},
      true, DwarfExprEval::Completion::kSync, 0u);
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_gt},
      true, DwarfExprEval::Completion::kSync, 0u);
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_gt},
      true, DwarfExprEval::Completion::kSync, 1u);
}

TEST_F(DwarfExprEvalTest, Le) {
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_le},
      true, DwarfExprEval::Completion::kSync, 1u);
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_le},
      true, DwarfExprEval::Completion::kSync, 1u);
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_le},
      true, DwarfExprEval::Completion::kSync, 0u);
}

TEST_F(DwarfExprEvalTest, Lt) {
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lt},
      true, DwarfExprEval::Completion::kSync, 0u);
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_lt},
      true, DwarfExprEval::Completion::kSync, 1u);
}

TEST_F(DwarfExprEvalTest, Ne) {
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_ne},
      true, DwarfExprEval::Completion::kSync, 0u);
  DoEvalTest(
      {llvm::dwarf::DW_OP_lit0, llvm::dwarf::DW_OP_lit1, llvm::dwarf::DW_OP_ne},
      true, DwarfExprEval::Completion::kSync, 1u);
}

}  // namespace zxdb
