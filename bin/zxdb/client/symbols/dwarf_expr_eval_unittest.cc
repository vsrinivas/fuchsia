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
  EXPECT_FALSE(eval_.is_complete());

  bool callback_issued = false;
  EXPECT_EQ(expected_completion,
            eval_.Eval(&provider_, data, [&callback_issued, expected_success,
                                          expected_completion, expected_result,
                                          expected_message](DwarfExprEval* eval,
                                                            const Err& err) {
              EXPECT_TRUE(eval->is_complete());
              EXPECT_EQ(expected_success, !err.has_error());
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

TEST_F(DwarfExprEvalTest, Constu) {
  // 129 in ULEB is 0x81, 0x01 (example in DWARF spec).
  DoEvalTest({llvm::dwarf::DW_OP_constu, 0x81, 0x01}, true,
             DwarfExprEval::Completion::kSync, 129u);
}

// TODO(brettw) test and breg and bregx.

}  // namespace zxdb
