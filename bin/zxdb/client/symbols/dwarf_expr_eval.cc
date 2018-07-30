// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_expr_eval.h"

#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/DataExtractor.h"

namespace zxdb {

DwarfExprEval::DwarfExprEval() : weak_factory_(this) {}
DwarfExprEval::~DwarfExprEval() = default;

uint64_t DwarfExprEval::GetResult() const {
  FXL_DCHECK(is_complete_);
  FXL_DCHECK(is_success_);
  FXL_DCHECK(!stack_.empty());
  return stack_.back();
}

DwarfExprEval::Completion DwarfExprEval::Eval(SymbolDataProvider* data_provider,
                                              Expression expr,
                                              CompletionCallback cb) {
  data_provider_ = data_provider;
  expr_ = std::move(expr);
  completion_callback_ = std::move(cb);

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
  do {
    // Check for successfully reaching the end of the stream.
    if (!is_complete_ && expr_index_ == expr_.size()) {
      is_complete_ = true;
      is_success_ = true;
      completion_callback_(this, Err());
      return;
    }
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
      return OpPushUnsigned(8);  // Assume 64-bit (8-bytes per address).
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
    case llvm::dwarf::DW_OP_drop:
    case llvm::dwarf::DW_OP_over:
    case llvm::dwarf::DW_OP_pick:  // 1-byte stack index.
    case llvm::dwarf::DW_OP_swap:
    case llvm::dwarf::DW_OP_rot:
    case llvm::dwarf::DW_OP_xderef:
    case llvm::dwarf::DW_OP_abs:
    case llvm::dwarf::DW_OP_and:
    case llvm::dwarf::DW_OP_div:
    case llvm::dwarf::DW_OP_minus:
    case llvm::dwarf::DW_OP_mod:
    case llvm::dwarf::DW_OP_mul:
    case llvm::dwarf::DW_OP_neg:
    case llvm::dwarf::DW_OP_not:
    case llvm::dwarf::DW_OP_or:
    case llvm::dwarf::DW_OP_plus:
    case llvm::dwarf::DW_OP_plus_uconst:  // ULEB128 addend.
    case llvm::dwarf::DW_OP_shl:
    case llvm::dwarf::DW_OP_shr:
    case llvm::dwarf::DW_OP_shra:
    case llvm::dwarf::DW_OP_xor:
    case llvm::dwarf::DW_OP_skip:  // Signed 2-byte constant.
    case llvm::dwarf::DW_OP_bra:   // Signed 2-byte constant.
    case llvm::dwarf::DW_OP_eq:
    case llvm::dwarf::DW_OP_ge:
    case llvm::dwarf::DW_OP_gt:
    case llvm::dwarf::DW_OP_le:
    case llvm::dwarf::DW_OP_lt:
    case llvm::dwarf::DW_OP_ne:
      ReportUnimplementedOpcode(op);
      return Completion::kSync;

    case llvm::dwarf::DW_OP_regx:
      return OpRegx();

    case llvm::dwarf::DW_OP_fbreg:        // SLEB128 constant.
                                          // This is an address relative to the
                                          // "frame base" a.k.a. stack pointer.
    case llvm::dwarf::DW_OP_bregx:
      return OpBregx();
    case llvm::dwarf::DW_OP_piece:        // ULEB128 size of piece addressed.
    case llvm::dwarf::DW_OP_deref_size:   // 1-byte size of data retrieved.
    case llvm::dwarf::DW_OP_xderef_size:  // 1-byte size of data retrieved.
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
    case llvm::dwarf::DW_OP_stack_value:
      ReportUnimplementedOpcode(op);
      return Completion::kSync;

    default:
      // Invalid or unknown opcode.
      ReportError(
          fxl::StringPrintf("Invalid opcode 0x%x in DWARF expression.", op));
      return Completion::kSync;
  }
}

DwarfExprEval::Completion DwarfExprEval::PushRegisterWithOffset(
    int dwarf_register_number, uint64_t offset) {
  uint64_t register_data = 0;
  if (data_provider_->GetRegister(dwarf_register_number, &register_data)) {
    // Register data available synchronously.
    Push(register_data + offset);
    return Completion::kSync;
  }

  // Must request async.
  data_provider_->GetRegisterAsync(dwarf_register_number, [
    weak_eval = weak_factory_.GetWeakPtr(), dwarf_register_number
  ](bool success, uint64_t value) {
    if (!weak_eval)
      return;
    if (!success) {
      weak_eval->ReportError(fxl::StringPrintf(
          "DWARF register %d is required but is not available.",
          dwarf_register_number));
      return;
    }
    weak_eval->Push(value);

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

void DwarfExprEval::ReportUnimplementedOpcode(uint8_t op) {
  ReportError(
      fxl::StringPrintf("Unimplemented opcode 0x%x in DWARF expression.", op));
}

// 1 parameter: SLEB128 offset added to base register.
DwarfExprEval::Completion DwarfExprEval::OpBreg(uint8_t op) {
  int reg_index = op - llvm::dwarf::DW_OP_breg0;
  int64_t offset = 0;
  if (!ReadLEBSigned(&offset))
    return Completion::kSync;
  return PushRegisterWithOffset(reg_index, offset);
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

}  // namespace zxdb
