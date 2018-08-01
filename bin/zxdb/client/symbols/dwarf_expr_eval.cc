// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_expr_eval.h"

#include <stdlib.h>

#include <utility>

#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/DataExtractor.h"

namespace zxdb {

DwarfExprEval::DwarfExprEval() : weak_factory_(this) {}
DwarfExprEval::~DwarfExprEval() = default;

DwarfExprEval::ResultType DwarfExprEval::GetResultType() const {
  FXL_DCHECK(is_complete_);
  FXL_DCHECK(is_success_);
  return result_type_;
}

uint64_t DwarfExprEval::GetResult() const {
  FXL_DCHECK(is_complete_);
  FXL_DCHECK(is_success_);
  return stack_.back();
}

DwarfExprEval::Completion DwarfExprEval::Eval(SymbolDataProvider* data_provider,
                                              Expression expr,
                                              CompletionCallback cb) {
  is_complete_ = false;
  data_provider_ = data_provider;
  expr_ = std::move(expr);
  expr_index_ = 0;
  completion_callback_ = std::move(cb);
  stack_.clear();

  if (!expr_.empty()) {
    // Assume 64-bit (8 bytes per address) little-endian.
    data_extractor_ = std::make_unique<llvm::DataExtractor>(
        llvm::StringRef(reinterpret_cast<const char*>(&expr_[0]), expr_.size()),
        true, 8);
  }

  ContinueEval();
  return is_complete_ ? Completion::kSync : Completion::kAsync;
}

void DwarfExprEval::ContinueEval() {
  // To allow interruption, only a certain number of instructions will be
  // executed in sequence without posting back to the message loop. This
  // gives calling code the chance to cancel long or hung executions. Since
  // most programs are 1-4 instructions, the threshold can be low.
  constexpr int kMaxInstructionsAtOnce = 32;
  int instruction_count = 0;

  do {
    // Check for successfully reaching the end of the stream.
    if (!is_complete_ && expr_index_ == expr_.size()) {
      is_complete_ = true;
      Err err;
      if (stack_.empty()) {
        // Failure to compute any values.
        err = Err("DWARF expression produced no results.");
        is_success_ = false;
      } else {
        is_success_ = true;
      }
      completion_callback_(this, err);
      completion_callback_ = CompletionCallback();
      return;
    }

    if (instruction_count == kMaxInstructionsAtOnce) {
      // Enough instructions have run at once. Schedule a callback to continue
      // execution in the message loop.
      debug_ipc::MessageLoop::Current()
          ->PostTask([weak_eval = weak_factory_.GetWeakPtr()]() {
            if (weak_eval)
              weak_eval->ContinueEval();
          });
      return;
    }
    instruction_count++;
  } while (!is_complete_ && EvalOneOp() == Completion::kSync);
}

DwarfExprEval::Completion DwarfExprEval::EvalOneOp() {
  FXL_DCHECK(!is_complete_);
  FXL_DCHECK(expr_index_ < expr_.size());

  // Opcode is next byte in the data buffer. Consume it.
  uint8_t op = expr_[expr_index_];
  expr_index_++;

  // Literals 0-31.
  if (op >= llvm::dwarf::DW_OP_lit0 && op <= llvm::dwarf::DW_OP_lit31) {
    Push(op - llvm::dwarf::DW_OP_lit0);
    return Completion::kSync;
  }

  // Registers 0-31.
  if (op >= llvm::dwarf::DW_OP_reg0 && op <= llvm::dwarf::DW_OP_reg31)
    return PushRegisterWithOffset(op - llvm::dwarf::DW_OP_reg0, 0);

  // Base register with SLEB128 offset.
  if (op >= llvm::dwarf::DW_OP_breg0 && op <= llvm::dwarf::DW_OP_breg31)
    return OpBreg(op);

  switch (op) {
    case llvm::dwarf::DW_OP_addr:
      // This is disabled until we have an example. The spec doesn't say
      // anything about what this is supposed to be relative to (most likely
      // the current module load address). It doesn't make sense for it to be
      // truely absolute since no absolute addresses will be known at build
      // time.
      // return OpPushUnsigned(8);  // Assume 64-bit (8-bytes per address).
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_const1u:
      return OpPushUnsigned(1);
    case llvm::dwarf::DW_OP_const1s:
      return OpPushSigned(1);
    case llvm::dwarf::DW_OP_const2u:
      return OpPushUnsigned(2);
    case llvm::dwarf::DW_OP_const2s:
      return OpPushSigned(2);
    case llvm::dwarf::DW_OP_const4u:
      return OpPushUnsigned(4);
    case llvm::dwarf::DW_OP_const4s:
      return OpPushSigned(4);
    case llvm::dwarf::DW_OP_const8u:
      return OpPushUnsigned(8);
    case llvm::dwarf::DW_OP_const8s:
      return OpPushSigned(8);
    case llvm::dwarf::DW_OP_constu:
      return OpPushLEBUnsigned();
    case llvm::dwarf::DW_OP_consts:
      return OpPushLEBSigned();
    case llvm::dwarf::DW_OP_dup:
      return OpDup();
    case llvm::dwarf::DW_OP_drop:
      return OpDrop();
    case llvm::dwarf::DW_OP_over:
      return OpOver();
    case llvm::dwarf::DW_OP_pick:
      return OpPick();
    case llvm::dwarf::DW_OP_swap:
      return OpSwap();
    case llvm::dwarf::DW_OP_rot:
      return OpRot();
    case llvm::dwarf::DW_OP_xderef:
      // TODO(brettw) implement this.
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_abs:
      return OpUnary([](uint64_t a) {
        return static_cast<uint64_t>(llabs(static_cast<long long>(a)));
      });
    case llvm::dwarf::DW_OP_and:
      return OpBinary([](uint64_t a, uint64_t b) { return a & b; });
    case llvm::dwarf::DW_OP_div:
      return OpDiv();
    case llvm::dwarf::DW_OP_minus:
      return OpBinary([](uint64_t a, uint64_t b) { return a - b; });
    case llvm::dwarf::DW_OP_mod:
      return OpMod();
    case llvm::dwarf::DW_OP_mul:
      return OpBinary([](uint64_t a, uint64_t b) { return a * b; });
    case llvm::dwarf::DW_OP_neg:
      return OpUnary([](uint64_t a) {
        return static_cast<uint64_t>(-static_cast<int64_t>(a));
      });
    case llvm::dwarf::DW_OP_not:
      return OpUnary([](uint64_t a) { return ~a; });
    case llvm::dwarf::DW_OP_or:
      return OpBinary([](uint64_t a, uint64_t b) { return a | b; });
    case llvm::dwarf::DW_OP_plus:
      return OpBinary([](uint64_t a, uint64_t b) { return a + b; });
    case llvm::dwarf::DW_OP_plus_uconst:
      return OpPlusUconst();
    case llvm::dwarf::DW_OP_shl:
      return OpBinary([](uint64_t a, uint64_t b) { return a << b; });
    case llvm::dwarf::DW_OP_shr:
      return OpBinary([](uint64_t a, uint64_t b) { return a >> b; });
    case llvm::dwarf::DW_OP_shra:
      return OpBinary([](uint64_t a, uint64_t b) {
        return static_cast<uint64_t>(static_cast<int64_t>(a) >>
                                     static_cast<int64_t>(b));
      });
    case llvm::dwarf::DW_OP_xor:
      return OpBinary([](uint64_t a, uint64_t b) { return a ^ b; });
    case llvm::dwarf::DW_OP_skip:
      return OpSkip();
    case llvm::dwarf::DW_OP_bra:
      return OpBra();
    case llvm::dwarf::DW_OP_eq:
      return OpBinary(
          [](uint64_t a, uint64_t b) { return static_cast<uint64_t>(a == b); });
    case llvm::dwarf::DW_OP_ge:
      return OpBinary(
          [](uint64_t a, uint64_t b) { return static_cast<uint64_t>(a >= b); });
    case llvm::dwarf::DW_OP_gt:
      return OpBinary(
          [](uint64_t a, uint64_t b) { return static_cast<uint64_t>(a > b); });
    case llvm::dwarf::DW_OP_le:
      return OpBinary(
          [](uint64_t a, uint64_t b) { return static_cast<uint64_t>(a <= b); });
    case llvm::dwarf::DW_OP_lt:
      return OpBinary(
          [](uint64_t a, uint64_t b) { return static_cast<uint64_t>(a < b); });
    case llvm::dwarf::DW_OP_ne:
      return OpBinary(
          [](uint64_t a, uint64_t b) { return static_cast<uint64_t>(a != b); });
    case llvm::dwarf::DW_OP_regx:
      return OpRegx();
    case llvm::dwarf::DW_OP_fbreg:
      // TODO(brettw) implement this.
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_bregx:
      return OpBregx();
    case llvm::dwarf::DW_OP_piece:
      // TODO(brettw) implement this.
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_deref_size:
      // TODO(brettw) implement this.
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_xderef_size:
      // TODO(brettw) implement this.
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_nop:
      return Completion::kSync;
    case llvm::dwarf::DW_OP_push_object_address:
    case llvm::dwarf::DW_OP_call2:     // 2-byte offset of DIE.
    case llvm::dwarf::DW_OP_call4:     // 4-byte offset of DIE.
    case llvm::dwarf::DW_OP_call_ref:  // 4- or 8-byte offset of DIE.
    case llvm::dwarf::DW_OP_form_tls_address:
    case llvm::dwarf::DW_OP_call_frame_cfa:
    case llvm::dwarf::DW_OP_bit_piece:       // ULEB128 size + ULEB128 offset.
    case llvm::dwarf::DW_OP_implicit_value:  // ULEB128 size + block of size.
      // TODO(brettw) implement these.
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_stack_value:
      return OpStackValue();

    default:
      // Invalid or unknown opcode.
      ReportError(
          fxl::StringPrintf("Invalid opcode 0x%x in DWARF expression.", op));
      return Completion::kSync;
  }
}

DwarfExprEval::Completion DwarfExprEval::PushRegisterWithOffset(
    int dwarf_register_number, int64_t offset) {
  uint64_t register_data = 0;
  if (data_provider_->GetRegister(dwarf_register_number, &register_data)) {
    // Register data available synchronously.
    Push(register_data + offset);
    return Completion::kSync;
  }

  // Must request async.
  data_provider_->GetRegisterAsync(dwarf_register_number, [
    weak_eval = weak_factory_.GetWeakPtr(), dwarf_register_number, offset
  ](bool success, uint64_t value) {
    if (!weak_eval)
      return;
    if (!success) {
      weak_eval->ReportError(fxl::StringPrintf(
          "DWARF register %d is required but is not available.",
          dwarf_register_number));
      return;
    }
    weak_eval->Push(static_cast<uint64_t>(value + offset));

    // Picks up processing at the next instruction.
    weak_eval->ContinueEval();
  });

  return Completion::kAsync;
}

void DwarfExprEval::Push(uint64_t value) { stack_.push_back(value); }

bool DwarfExprEval::ReadSigned(int byte_size, int64_t* output) {
  uint32_t old_expr_index = expr_index_;
  *output = data_extractor_->getSigned(&expr_index_, byte_size);
  if (old_expr_index == expr_index_) {
    ReportError("Bad number format in DWARF expression.");
    return false;
  }
  return true;
}

bool DwarfExprEval::ReadUnsigned(int byte_size, uint64_t* output) {
  uint32_t old_expr_index = expr_index_;
  *output = data_extractor_->getUnsigned(&expr_index_, byte_size);
  if (old_expr_index == expr_index_) {
    ReportError("Bad number format in DWARF expression.");
    return false;
  }
  return true;
}

bool DwarfExprEval::ReadLEBSigned(int64_t* output) {
  uint32_t old_expr_index = expr_index_;
  *output = data_extractor_->getSLEB128(&expr_index_);
  if (old_expr_index == expr_index_) {
    ReportError("Bad number format in DWARF expression.");
    return false;
  }
  return true;
}

bool DwarfExprEval::ReadLEBUnsigned(uint64_t* output) {
  uint32_t old_expr_index = expr_index_;
  *output = data_extractor_->getULEB128(&expr_index_);
  if (old_expr_index == expr_index_) {
    ReportError("Bad number format in DWARF expression.");
    return false;
  }
  return true;
}

void DwarfExprEval::ReportError(const std::string& msg) {
  is_complete_ = true;
  completion_callback_(this, Err(msg));

  // The callback should only be called once, so force accidental future uses
  // to fail.
  completion_callback_ = CompletionCallback();
}

void DwarfExprEval::ReportStackUnderflow() {
  ReportError("Stack underflow for DWARF expression.");
}

void DwarfExprEval::ReportUnimplementedOpcode(uint8_t op) {
  ReportError(
      fxl::StringPrintf("Unimplemented opcode 0x%x in DWARF expression.", op));
}

DwarfExprEval::Completion DwarfExprEval::OpUnary(uint64_t (*op)(uint64_t)) {
  if (stack_.empty())
    ReportStackUnderflow();
  else
    stack_.back() = op(stack_.back());
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpBinary(uint64_t (*op)(uint64_t,
                                                                 uint64_t)) {
  if (stack_.size() < 2) {
    ReportStackUnderflow();
  } else {
    uint64_t b = stack_.back();
    stack_.pop_back();
    uint64_t a = stack_.back();
    stack_.back() = op(a, b);
  }
  return Completion::kSync;
}

// 1 parameter: 2 byte signed integer constant.
DwarfExprEval::Completion DwarfExprEval::OpBra() {
  // "The 2-byte constant is the number of bytes of the DWARF expression to skip
  // forward or backward from the current operation, beginning after the 2-byte
  // constant."
  int64_t skip_amount = 0;
  if (!ReadSigned(2, &skip_amount))
    return Completion::kSync;

  if (stack_.empty()) {
    ReportStackUnderflow();
    return Completion::kSync;
  }

  // 0 @ top of stack means don't take the branch.
  uint64_t condition = stack_.back();
  stack_.pop_back();
  if (condition == 0)
    return Completion::kSync;

  // Otherwise take the branch.
  Skip(skip_amount);
  return Completion::kSync;
}

// 1 parameter: SLEB128 offset added to base register.
DwarfExprEval::Completion DwarfExprEval::OpBreg(uint8_t op) {
  int reg_index = op - llvm::dwarf::DW_OP_breg0;
  int64_t offset = 0;
  if (!ReadLEBSigned(&offset))
    return Completion::kSync;
  return PushRegisterWithOffset(reg_index, offset);
}

DwarfExprEval::Completion DwarfExprEval::OpDiv() {
  if (stack_.size() < 2) {
    ReportStackUnderflow();
  } else {
    uint64_t b = stack_.back();
    stack_.pop_back();
    uint64_t a = stack_.back();

    if (b == 0) {
      ReportError("DWARF expression divided by zero.");
    } else {
      stack_.back() = static_cast<uint64_t>(static_cast<int64_t>(a) /
                                            static_cast<int64_t>(b));
    }
  }
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpDrop() {
  if (stack_.empty())
    ReportStackUnderflow();
  else
    stack_.pop_back();
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpDup() {
  if (stack_.empty())
    ReportStackUnderflow();
  else
    stack_.push_back(stack_.back());
  return Completion::kSync;
}

// 1 parameter: ULEB128 constant indexing the register.
DwarfExprEval::Completion DwarfExprEval::OpRegx() {
  uint64_t reg = 0;
  if (!ReadLEBUnsigned(&reg))
    return Completion::kSync;
  return PushRegisterWithOffset(static_cast<int>(reg), 0);
}

// 2 parameters: ULEB128 register number + SLEB128 offset.
DwarfExprEval::Completion DwarfExprEval::OpBregx() {
  uint64_t reg = 0;
  if (!ReadLEBUnsigned(&reg))
    return Completion::kSync;

  int64_t offset = 0;
  if (!ReadLEBSigned(&offset))
    return Completion::kSync;

  return PushRegisterWithOffset(static_cast<int>(reg), offset);
}

DwarfExprEval::Completion DwarfExprEval::OpMod() {
  if (stack_.size() < 2) {
    ReportStackUnderflow();
  } else {
    uint64_t b = stack_.back();
    stack_.pop_back();
    uint64_t a = stack_.back();

    if (b == 0) {
      ReportError("DWARF expression divided by zero.");
    } else {
      stack_.back() = static_cast<uint64_t>(static_cast<int64_t>(a) %
                                            static_cast<int64_t>(b));
    }
  }
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpOver() {
  // Duplicates the next-to-top over the top item.
  if (stack_.size() < 2)
    ReportStackUnderflow();
  else
    Push(stack_[stack_.size() - 2]);
  return Completion::kSync;
}

// 1 parameter: 1-byte stack index from the top to push.
DwarfExprEval::Completion DwarfExprEval::OpPick() {
  uint64_t index = 0;
  if (!ReadUnsigned(1, &index))
    return Completion::kSync;

  if (stack_.size() <= index) {
    ReportStackUnderflow();
    return Completion::kSync;
  }

  // Index is from end (0 = last item).
  Push(stack_[stack_.size() - 1 - index]);
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPlusUconst() {
  // "Pops the top stack entry, adds it to the unsigned LEB128 constant operand
  // and pushes the result."
  if (stack_.empty()) {
    ReportStackUnderflow();
  } else {
    uint64_t top = stack_.back();
    stack_.pop_back();

    uint64_t param = 0;
    if (ReadLEBUnsigned(&param))
      Push(top + param);
  }
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushSigned(int byte_count) {
  int64_t value = 0;
  if (ReadSigned(byte_count, &value))
    Push(static_cast<uint64_t>(value));
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushUnsigned(int byte_count) {
  uint64_t value = 0;
  if (ReadUnsigned(byte_count, &value))
    Push(value);
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushLEBSigned() {
  int64_t value = 0;
  if (ReadLEBSigned(&value))
    Push(static_cast<uint64_t>(value));
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushLEBUnsigned() {
  uint64_t value = 0;
  if (ReadLEBUnsigned(&value))
    Push(value);
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpRot() {
  // Rotates the top 3 entries "down" with wraparound. "The entry at the top of
  // the stack becomes the third stack entry, the second entry becomes the top
  // of the stack, and the third entry becomes the second entry."
  if (stack_.size() < 3) {
    ReportStackUnderflow();
  } else {
    uint64_t top = stack_[stack_.size() - 1];
    uint64_t one_back = stack_[stack_.size() - 2];
    uint64_t two_back = stack_[stack_.size() - 3];

    stack_[stack_.size() - 1] = one_back;
    stack_[stack_.size() - 2] = two_back;
    stack_[stack_.size() - 3] = top;
  }
  return Completion::kSync;
}

// 1 parameter: 2-byte signed constant.
DwarfExprEval::Completion DwarfExprEval::OpSkip() {
  int64_t skip_amount = 0;
  if (!ReadSigned(2, &skip_amount))
    return Completion::kSync;
  Skip(skip_amount);
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpStackValue() {
  // "Specifies that the object does not exist in memory but rather is a
  // constant value. The value from the top of the stack is the value to be
  // used. This is the actual object value and not the location."
  result_type_ = ResultType::kValue;

  // This operation also implicitly terminates the computation. Jump to the
  // end to indicate this.
  expr_index_ = expr_.size();

  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpSwap() {
  if (stack_.size() < 2)
    ReportStackUnderflow();
  else
    std::swap(stack_[stack_.size() - 1], stack_[stack_.size() - 2]);
  return Completion::kSync;
}

void DwarfExprEval::Skip(int64_t amount) {
  int64_t new_index = static_cast<int64_t>(expr_index_) + amount;
  if (new_index >= static_cast<int64_t>(expr_.size())) {
    // Skip to or past the end just terminates the program.
    expr_index_ = expr_.size();
  } else if (new_index < 0) {
    // Skip before beginning is an error.
    ReportError("DWARF expression skips out-of-bounds.");
  } else {
    expr_index_ = static_cast<uint32_t>(new_index);
  }
}

}  // namespace zxdb
