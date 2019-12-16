// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"

#include <inttypes.h>
#include <stdlib.h>

#include <utility>

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/DataExtractor.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

DwarfExprEval::DwarfExprEval()
    : symbol_context_(SymbolContext::ForRelativeAddresses()), weak_factory_(this) {}

DwarfExprEval::~DwarfExprEval() {
  // This assertion verifies that this class was not accidentally deleted from
  // within the completion callback. This class is not set up to handle this
  // case.
  FXL_CHECK(!in_completion_callback_);
}

void DwarfExprEval::Push(StackEntry value) { stack_.push_back(value); }

DwarfExprEval::ResultType DwarfExprEval::GetResultType() const {
  FXL_DCHECK(is_complete_);
  FXL_DCHECK(is_success_);

  if (!result_data_.empty())
    return ResultType::kData;
  return result_type_;
}

DwarfExprEval::StackEntry DwarfExprEval::GetResult() const {
  FXL_DCHECK(is_complete_);
  FXL_DCHECK(is_success_);
  return stack_.back();
}

DwarfExprEval::Completion DwarfExprEval::Eval(fxl::RefPtr<SymbolDataProvider> data_provider,
                                              const SymbolContext& symbol_context, Expression expr,
                                              CompletionCallback cb) {
  is_complete_ = false;
  data_provider_ = std::move(data_provider);
  symbol_context_ = symbol_context;
  expr_ = std::move(expr);
  expr_index_ = 0;
  completion_callback_ = std::move(cb);

  if (!expr_.empty()) {
    // Assume little-endian.
    data_extractor_ = std::make_unique<llvm::DataExtractor>(
        llvm::StringRef(reinterpret_cast<const char*>(&expr_[0]), expr_.size()), true,
        kTargetPointerSize);
  }

  // Note: ContinueEval() may call callback, which may delete |this|
  return ContinueEval() ? Completion::kSync : Completion::kAsync;
}

bool DwarfExprEval::ContinueEval() {
  // To allow interruption, only a certain number of instructions will be
  // executed in sequence without posting back to the message loop. This
  // gives calling code the chance to cancel long or hung executions. Since
  // most programs are 1-4 instructions, the threshold can be low.
  constexpr int kMaxInstructionsAtOnce = 32;
  int instruction_count = 0;

  do {
    // Check for successfully reaching the end of the stream.
    if (!is_complete_ && expr_index_ == expr_.size()) {
      data_provider_.reset();
      is_complete_ = true;
      Err err;
      if (stack_.empty() && result_data_.empty()) {
        // Failure to compute any values.
        err = Err("DWARF expression produced no results.");
        is_success_ = false;
      } else {
        is_success_ = true;
      }

      in_completion_callback_ = true;
      completion_callback_(this, err);
      completion_callback_ = {};
      in_completion_callback_ = false;
      return is_complete_;
    }

    if (instruction_count == kMaxInstructionsAtOnce) {
      // Enough instructions have run at once. Schedule a callback to continue
      // execution in the message loop.
      debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE,
                                                  [weak_eval = weak_factory_.GetWeakPtr()]() {
                                                    if (weak_eval)
                                                      weak_eval->ContinueEval();
                                                  });
      return is_complete_;
    }
    instruction_count++;
  } while (!is_complete_ && EvalOneOp() == Completion::kSync);
  return is_complete_;
}

DwarfExprEval::Completion DwarfExprEval::EvalOneOp() {
  FXL_DCHECK(!is_complete_);
  FXL_DCHECK(expr_index_ < expr_.size());

  // Clear any current register information. See current_register_id_ declaration for more.
  current_register_id_ = debug_ipc::RegisterID::kUnknown;

  // Opcode is next byte in the data buffer. Consume it.
  uint8_t op = expr_[expr_index_];
  expr_index_++;

  // Literals 0-31.
  if (op >= llvm::dwarf::DW_OP_lit0 && op <= llvm::dwarf::DW_OP_lit31) {
    Push(op - llvm::dwarf::DW_OP_lit0);
    return Completion::kSync;
  }

  // Registers 0-31.
  if (op >= llvm::dwarf::DW_OP_reg0 && op <= llvm::dwarf::DW_OP_reg31) {
    result_type_ = ResultType::kValue;
    return PushRegisterWithOffset(op - llvm::dwarf::DW_OP_reg0, 0);
  }

  // Base register with SLEB128 offset.
  if (op >= llvm::dwarf::DW_OP_breg0 && op <= llvm::dwarf::DW_OP_breg31)
    return OpBreg(op);

  switch (op) {
    case llvm::dwarf::DW_OP_addr:
      return OpAddr();
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
      // We don't have multiple address spaces.
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_abs:
      return OpUnary(
          [](StackEntry a) { return static_cast<StackEntry>(llabs(static_cast<long long>(a))); });
    case llvm::dwarf::DW_OP_and:
      return OpBinary([](StackEntry a, StackEntry b) { return a & b; });
    case llvm::dwarf::DW_OP_div:
      return OpDiv();
    case llvm::dwarf::DW_OP_minus:
      return OpBinary([](StackEntry a, StackEntry b) { return a - b; });
    case llvm::dwarf::DW_OP_mod:
      return OpMod();
    case llvm::dwarf::DW_OP_mul:
      return OpBinary([](StackEntry a, StackEntry b) { return a * b; });
    case llvm::dwarf::DW_OP_neg:
      return OpUnary(
          [](StackEntry a) { return static_cast<StackEntry>(-static_cast<SignedStackEntry>(a)); });
    case llvm::dwarf::DW_OP_not:
      return OpUnary([](StackEntry a) { return ~a; });
    case llvm::dwarf::DW_OP_or:
      return OpBinary([](StackEntry a, StackEntry b) { return a | b; });
    case llvm::dwarf::DW_OP_plus:
      return OpBinary([](StackEntry a, StackEntry b) { return a + b; });
    case llvm::dwarf::DW_OP_plus_uconst:
      return OpPlusUconst();
    case llvm::dwarf::DW_OP_shl:
      return OpBinary([](StackEntry a, StackEntry b) { return a << b; });
    case llvm::dwarf::DW_OP_shr:
      return OpBinary([](StackEntry a, StackEntry b) { return a >> b; });
    case llvm::dwarf::DW_OP_shra:
      return OpBinary([](StackEntry a, StackEntry b) {
        return static_cast<StackEntry>(static_cast<SignedStackEntry>(a) >>
                                       static_cast<SignedStackEntry>(b));
      });
    case llvm::dwarf::DW_OP_xor:
      return OpBinary([](StackEntry a, StackEntry b) { return a ^ b; });
    case llvm::dwarf::DW_OP_skip:
      return OpSkip();
    case llvm::dwarf::DW_OP_bra:
      return OpBra();
    case llvm::dwarf::DW_OP_eq:
      return OpBinary([](StackEntry a, StackEntry b) { return static_cast<StackEntry>(a == b); });
    case llvm::dwarf::DW_OP_ge:
      return OpBinary([](StackEntry a, StackEntry b) { return static_cast<StackEntry>(a >= b); });
    case llvm::dwarf::DW_OP_gt:
      return OpBinary([](StackEntry a, StackEntry b) { return static_cast<StackEntry>(a > b); });
    case llvm::dwarf::DW_OP_le:
      return OpBinary([](StackEntry a, StackEntry b) { return static_cast<StackEntry>(a <= b); });
    case llvm::dwarf::DW_OP_lt:
      return OpBinary([](StackEntry a, StackEntry b) { return static_cast<StackEntry>(a < b); });
    case llvm::dwarf::DW_OP_ne:
      return OpBinary([](StackEntry a, StackEntry b) { return static_cast<StackEntry>(a != b); });
    case llvm::dwarf::DW_OP_regx:
      return OpRegx();
    case llvm::dwarf::DW_OP_fbreg:
      return OpFbreg();
    case llvm::dwarf::DW_OP_bregx:
      return OpBregx();
    case llvm::dwarf::DW_OP_piece:
      return OpPiece();
    case llvm::dwarf::DW_OP_deref:
      return OpDeref(sizeof(TargetPointer));
    case llvm::dwarf::DW_OP_deref_size:
      return OpDerefSize();
    case llvm::dwarf::DW_OP_xderef_size:
      // We don't have multiple address spaces.
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_nop:
      return Completion::kSync;
    case llvm::dwarf::DW_OP_push_object_address:
    case llvm::dwarf::DW_OP_call2:     // 2-byte offset of DIE.
    case llvm::dwarf::DW_OP_call4:     // 4-byte offset of DIE.
    case llvm::dwarf::DW_OP_call_ref:  // 4- or 8-byte offset of DIE.
    case llvm::dwarf::DW_OP_form_tls_address:
      // TODO(brettw) implement these.
      ReportUnimplementedOpcode(op);
      return Completion::kSync;
    case llvm::dwarf::DW_OP_call_frame_cfa:
      return OpCFA();
    case llvm::dwarf::DW_OP_bit_piece:  // ULEB128 size + ULEB128 offset.
      // Clang will generate bit_piece operations to make 80-bit long double constants, but
      // the expressions are invalid: https://bugs.llvm.org/show_bug.cgi?id=43682
      // We were able to get GCC to generate a piece operation for:
      //   void foo(int x, int y) {
      //     struct { int x:3, :3, y:3; } s = {x, y};
      //   }
      // That also seems invalid. So we're waiting for a clearly valid example in the wild before
      // spending time trying to implement this.
      ReportError(
          "The DWARF encoding for this symbol uses DW_OP_bit_piece which is unimplemented.\n"
          "Please file a bit with a repro case so we can implement it properly.");
      return Completion::kSync;
    case llvm::dwarf::DW_OP_implicit_value:  // ULEB128 size + block of size.
      return OpImplicitValue();
    case llvm::dwarf::DW_OP_stack_value:
      return OpStackValue();
    case llvm::dwarf::DW_OP_GNU_push_tls_address:
      // TODO(DX-694) support TLS.
      ReportError("TLS not currently supported. See DX-694.");
      return Completion::kSync;

    case llvm::dwarf::DW_OP_implicit_pointer:
    case 0xf2:  // DW_OP_GNU_implicit_pointer (pre-DWARF5 GNU extension for this).
      // GCC generates this when a pointer has been optimized out, but it still can provide the
      // value of the thing that it pointed to. We don't implement this.
      ReportError("Optimized out (DW_OP_implicit_pointer)");
      return Completion::kSync;

    case 0xf3:  // DW_OP_GNU_entry_value
      // This GNU extension is a ULEB128 length followed by a sub-expression
      // of that length. This sub-expression is supposed to be evaluated in
      // a separate stack using the register values that were present at the
      // beginning of the function:
      // https://gcc.gnu.org/ml/gcc-patches/2010-08/txt00152.txt
      //
      // Generally if the registers were saved registers it would just encode
      // those locations. This is really used for non-saved registers and
      // requires that the debugger have previously saved those registers
      // separately. This isn't something that we currently do, and can't be
      // done in general (it could be implemented if you previously single-
      // stepped into that function though).
      ReportError("Optimized out (DW_OP_GNU_entry_value)");
      return Completion::kSync;

    default:
      // Invalid or unknown opcode.
      ReportError(fxl::StringPrintf("Invalid opcode 0x%x in DWARF expression.", op));
      return Completion::kSync;
  }
}

DwarfExprEval::Completion DwarfExprEval::PushRegisterWithOffset(int dwarf_register_number,
                                                                SignedStackEntry offset) {
  // Reading register data means the result is not constant.
  result_is_constant_ = false;

  const debug_ipc::RegisterInfo* reg_info =
      debug_ipc::DWARFToRegisterInfo(data_provider_->GetArch(), dwarf_register_number);
  if (!reg_info) {
    ReportError(fxl::StringPrintf("Register %d not known.", dwarf_register_number));
    return Completion::kSync;
  }

  // This function doesn't set the result_type_ because it is called from different contexts. The
  // callers should set the result_type_ as appropriate for their operation.
  if (auto reg_data = data_provider_->GetRegister(reg_info->id)) {
    // State known synchronously (could be available or known unavailable).
    if (reg_data->empty()) {
      ReportError(fxl::StringPrintf("Register %d not available.", dwarf_register_number));
    } else {
      // This truncates to 128 bits and converts from little-endian. DWARF doesn't seem to use the
      // stack machine for vector computations (it's not specified that the stack items are large
      // enough). When it uses a stack register for a floating-point scalar computation, it just
      // uses the low bits.
      StackEntry reg_value = 0;
      memcpy(&reg_value, reg_data->data(), std::min(sizeof(StackEntry), reg_data->size()));
      Push(reg_value + offset);

      // When the current value represents a register, save that fact.
      if (offset == 0)
        current_register_id_ = reg_info->id;
    }
    return Completion::kSync;
  }

  // Must request async.
  data_provider_->GetRegisterAsync(
      reg_info->id, [reg_id = reg_info->id, weak_eval = weak_factory_.GetWeakPtr(), offset](
                        const Err& err, std::vector<uint8_t> reg_data) {
        if (!weak_eval)
          return;
        if (err.has_error()) {
          weak_eval->ReportError(err);
          return;
        }

        // Truncate/convert from little-endian as above.
        StackEntry reg_value = 0;
        memcpy(&reg_value, reg_data.data(), std::min(sizeof(StackEntry), reg_data.size()));
        weak_eval->Push(static_cast<StackEntry>(reg_value + offset));

        // When the current value represents a register, save that fact.
        if (offset == 0)
          weak_eval->current_register_id_ = reg_id;

        // Picks up processing at the next instruction.
        weak_eval->ContinueEval();
      });

  return Completion::kAsync;
}

bool DwarfExprEval::ReadSigned(int byte_size, SignedStackEntry* output) {
  uint32_t old_expr_index = expr_index_;
  *output = (SignedStackEntry)(int64_t)data_extractor_->getSigned(&expr_index_, byte_size);
  if (old_expr_index == expr_index_) {
    ReportError("Bad number format in DWARF expression.");
    return false;
  }
  return true;
}

bool DwarfExprEval::ReadUnsigned(int byte_size, StackEntry* output) {
  uint32_t old_expr_index = expr_index_;
  *output = data_extractor_->getUnsigned(&expr_index_, byte_size);
  if (old_expr_index == expr_index_) {
    ReportError("Bad number format in DWARF expression.");
    return false;
  }
  return true;
}

bool DwarfExprEval::ReadLEBSigned(SignedStackEntry* output) {
  uint32_t old_expr_index = expr_index_;
  *output = data_extractor_->getSLEB128(&expr_index_);
  if (old_expr_index == expr_index_) {
    ReportError("Bad number format in DWARF expression.");
    return false;
  }
  return true;
}

bool DwarfExprEval::ReadLEBUnsigned(StackEntry* output) {
  uint32_t old_expr_index = expr_index_;
  *output = data_extractor_->getULEB128(&expr_index_);
  if (old_expr_index == expr_index_) {
    ReportError("Bad number format in DWARF expression.");
    return false;
  }
  return true;
}

void DwarfExprEval::ReadMemory(
    TargetPointer address, uint32_t byte_size,
    fit::callback<void(DwarfExprEval* eval, std::vector<uint8_t> value)> on_success) {
  // Reading memory means the result is not constant.
  result_is_constant_ = false;

  data_provider_->GetMemoryAsync(
      address, byte_size,
      [address, byte_size, weak_eval = weak_factory_.GetWeakPtr(),
       on_success = std::move(on_success)](const Err& err, std::vector<uint8_t> value) mutable {
        if (!weak_eval) {
          return;
        } else if (err.has_error()) {
          weak_eval->ReportError(err);
        } else if (value.size() != byte_size) {
          weak_eval->ReportError(
              fxl::StringPrintf("Invalid pointer 0x%" PRIx64 ".", static_cast<uint64_t>(address)));
        } else {
          on_success(weak_eval.get(), std::move(value));

          // Picks up processing at the next instruction.
          weak_eval->ContinueEval();
        }
      });
}

void DwarfExprEval::ReportError(const std::string& msg) { ReportError(Err(msg)); }

void DwarfExprEval::ReportError(const Err& err) {
  data_provider_.reset();
  is_complete_ = true;

  // Wrap completion callback with the flag to catch deletions from within the
  // callback.
  in_completion_callback_ = true;
  completion_callback_(this, err);
  completion_callback_ = {};
  in_completion_callback_ = false;
}

void DwarfExprEval::ReportStackUnderflow() { ReportError("Stack underflow for DWARF expression."); }

void DwarfExprEval::ReportUnimplementedOpcode(uint8_t op) {
  ReportError(fxl::StringPrintf("Unimplemented opcode 0x%x in DWARF expression.", op));
}

DwarfExprEval::Completion DwarfExprEval::OpUnary(StackEntry (*op)(StackEntry)) {
  if (stack_.empty())
    ReportStackUnderflow();
  else
    stack_.back() = op(stack_.back());
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpBinary(StackEntry (*op)(StackEntry, StackEntry)) {
  if (stack_.size() < 2) {
    ReportStackUnderflow();
  } else {
    StackEntry b = stack_.back();
    stack_.pop_back();
    StackEntry a = stack_.back();
    stack_.back() = op(a, b);
  }
  return Completion::kSync;
}

// 1 parameter: unsigned the size of a pointer. This is relative to the load
// address of the current module. It is used to for globals and statics.
DwarfExprEval::Completion DwarfExprEval::OpAddr() {
  StackEntry offset;
  if (!ReadUnsigned(kTargetPointerSize, &offset))
    return Completion::kSync;

  Push(symbol_context_.RelativeToAbsolute(static_cast<TargetPointer>(offset)));
  return Completion::kSync;
}

// 1 parameter: 2 byte signed integer constant.
DwarfExprEval::Completion DwarfExprEval::OpBra() {
  // "The 2-byte constant is the number of bytes of the DWARF expression to skip
  // forward or backward from the current operation, beginning after the 2-byte
  // constant."
  SignedStackEntry skip_amount = 0;
  if (!ReadSigned(2, &skip_amount))
    return Completion::kSync;

  if (stack_.empty()) {
    ReportStackUnderflow();
    return Completion::kSync;
  }

  // 0 @ top of stack means don't take the branch.
  StackEntry condition = stack_.back();
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
  SignedStackEntry offset = 0;
  if (!ReadLEBSigned(&offset))
    return Completion::kSync;

  result_type_ = ResultType::kPointer;
  return PushRegisterWithOffset(reg_index, offset);
}

DwarfExprEval::Completion DwarfExprEval::OpCFA() {
  // Reading the CFA means the result is not constant.
  result_is_constant_ = false;

  if (StackEntry cfa = data_provider_->GetCanonicalFrameAddress())
    Push(cfa);
  else
    ReportError("Frame address is 0.");
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpDiv() {
  if (stack_.size() < 2) {
    ReportStackUnderflow();
  } else {
    StackEntry b = stack_.back();
    stack_.pop_back();
    StackEntry a = stack_.back();

    if (b == 0) {
      ReportError("DWARF expression divided by zero.");
    } else {
      stack_.back() = static_cast<StackEntry>(static_cast<SignedStackEntry>(a) /
                                              static_cast<SignedStackEntry>(b));
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

// 1 parameter: Signed LEB128 offset from frame base pointer.
DwarfExprEval::Completion DwarfExprEval::OpFbreg() {
  // Reading the frame base means the result is not constant.
  result_is_constant_ = false;

  SignedStackEntry offset = 0;
  if (!ReadLEBSigned(&offset))
    return Completion::kSync;

  if (auto bp = data_provider_->GetFrameBase()) {
    // Available synchronously.

    // Certain problems can cause the BP to be set to 0 which is obviously
    // invalid, report that error specifically.
    if (*bp == 0)
      ReportError("Base Pointer is 0, can't evaluate.");

    result_type_ = ResultType::kPointer;
    Push(*bp + offset);
    return Completion::kSync;
  }

  // Must request async.
  data_provider_->GetFrameBaseAsync(
      [weak_eval = weak_factory_.GetWeakPtr(), offset](const Err& err, StackEntry value) {
        if (!weak_eval)
          return;
        if (err.has_error()) {
          weak_eval->ReportError(err);
          return;
        }

        if (value == 0) {
          weak_eval->ReportError("Base Pointer is 0, can't evaluate.");
          return;
        }

        weak_eval->result_type_ = ResultType::kPointer;
        weak_eval->Push(static_cast<StackEntry>(value + offset));

        // Picks up processing at the next instruction.
        weak_eval->ContinueEval();
      });

  return Completion::kAsync;
}

// 2 parameters: ULEB128 length, followed by that much data (in machine-endianness).
DwarfExprEval::Completion DwarfExprEval::OpImplicitValue() {
  StackEntry len = 0;
  if (!ReadLEBUnsigned(&len))
    return Completion::kSync;
  if (len > sizeof(StackEntry) || expr_index_ + static_cast<size_t>(len) > expr_.size()) {
    ReportError(fxl::StringPrintf("DWARF implicit value length too long: 0x%x.",
                                  static_cast<unsigned>(len)));
    return Completion::kSync;
  }

  StackEntry value = 0;
  if (len > 0) {
    memcpy(&value, &expr_[expr_index_], static_cast<size_t>(len));
    Skip(len);
  }

  Push(value);
  result_type_ = ResultType::kValue;
  return Completion::kSync;
}

// 1 parameter: ULEB128 constant indexing the register.
DwarfExprEval::Completion DwarfExprEval::OpRegx() {
  StackEntry reg = 0;
  if (!ReadLEBUnsigned(&reg))
    return Completion::kSync;

  result_type_ = ResultType::kValue;
  return PushRegisterWithOffset(static_cast<int>(reg), 0);
}

// 2 parameters: ULEB128 register number + SLEB128 offset.
DwarfExprEval::Completion DwarfExprEval::OpBregx() {
  StackEntry reg = 0;
  if (!ReadLEBUnsigned(&reg))
    return Completion::kSync;

  SignedStackEntry offset = 0;
  if (!ReadLEBSigned(&offset))
    return Completion::kSync;

  result_type_ = ResultType::kPointer;
  return PushRegisterWithOffset(static_cast<int>(reg), offset);
}

// Pops the stack and pushes an given-sized value from memory at that location.
DwarfExprEval::Completion DwarfExprEval::OpDeref(uint32_t byte_size) {
  if (stack_.empty()) {
    ReportStackUnderflow();
    return Completion::kSync;
  }

  if (byte_size == 0 || byte_size > sizeof(StackEntry)) {
    ReportError(fxl::StringPrintf("Invalid DWARF expression read size: %u", byte_size));
    return Completion::kSync;
  }

  StackEntry addr = stack_.back();
  stack_.pop_back();
  ReadMemory(addr, byte_size, [](DwarfExprEval* eval, std::vector<uint8_t> data) {
    // Success. This assumes little-endian and copies starting from the low bytes. The data will
    // have already been validated to be the correct size so we know it will fit in a StackEntry.
    FXL_DCHECK(data.size() <= sizeof(StackEntry));
    StackEntry to_push = 0;
    memcpy(&to_push, &data[0], data.size());
    eval->Push(to_push);
  });
  return Completion::kAsync;
}

DwarfExprEval::Completion DwarfExprEval::OpDerefSize() {
  // The operand is a 1-byte unsigned constant following the opcode.
  StackEntry byte_size = 0;
  if (!ReadUnsigned(1, &byte_size))
    return Completion::kSync;

  // The generic deref path can handle the rest.
  return OpDeref(static_cast<uint32_t>(byte_size));
}

DwarfExprEval::Completion DwarfExprEval::OpMod() {
  if (stack_.size() < 2) {
    ReportStackUnderflow();
  } else {
    StackEntry b = stack_.back();
    stack_.pop_back();
    StackEntry a = stack_.back();

    if (b == 0) {
      ReportError("DWARF expression divided by zero.");
    } else {
      stack_.back() = static_cast<StackEntry>(static_cast<SignedStackEntry>(a) %
                                              static_cast<SignedStackEntry>(b));
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
  StackEntry index = 0;
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

// 1 paramter: ULEB size of item in bytes.
DwarfExprEval::Completion DwarfExprEval::OpPiece() {
  StackEntry byte_size = 0;
  if (!ReadLEBUnsigned(&byte_size))
    return Completion::kSync;

  if (stack_.empty()) {
    ReportStackUnderflow();
    return Completion::kSync;
  }

  StackEntry source = stack_.back();
  stack_.pop_back();

  if (result_type_ == ResultType::kValue) {
    // Simple case where the source of the "piece" is the value at the top of the stack.
    if (byte_size > sizeof(StackEntry)) {
      ReportError(fxl::StringPrintf("DWARF expression listed a data size of %d which is too large.",
                                    static_cast<int>(byte_size)));
      return Completion::kSync;
    }

    // We want the low bytes, this assumes little-endian.
    uint8_t source_as_bytes[sizeof(StackEntry)];
    memcpy(&source_as_bytes, &source, sizeof(StackEntry));
    result_data_.insert(result_data_.end(), std::begin(source_as_bytes),
                        &source_as_bytes[byte_size]);

    // Reset the expression state to start a new one.
    result_type_ = ResultType::kPointer;
    return Completion::kSync;
  }

  // This is the more complex case where the top of the stack is a pointer to the value in memory.
  // We read that many bytes from memory and add it to the result data.
  ReadMemory(source, byte_size, [](DwarfExprEval* eval, std::vector<uint8_t> data) {
    // Success. Copy to the result.
    eval->result_data_.insert(eval->result_data_.end(), data.begin(), data.end());

    // Reset the expression state to start a new one.
    eval->result_type_ = ResultType::kPointer;
  });

  // The ReadMemory call will complete asynchronously.
  return Completion::kAsync;
}

DwarfExprEval::Completion DwarfExprEval::OpPlusUconst() {
  // "Pops the top stack entry, adds it to the unsigned LEB128 constant operand
  // and pushes the result."
  if (stack_.empty()) {
    ReportStackUnderflow();
  } else {
    StackEntry top = stack_.back();
    stack_.pop_back();

    StackEntry param = 0;
    if (ReadLEBUnsigned(&param))
      Push(top + param);
  }
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushSigned(int byte_count) {
  SignedStackEntry value = 0;
  if (ReadSigned(byte_count, &value))
    Push(static_cast<StackEntry>(value));
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushUnsigned(int byte_count) {
  StackEntry value = 0;
  if (ReadUnsigned(byte_count, &value))
    Push(value);
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushLEBSigned() {
  SignedStackEntry value = 0;
  if (ReadLEBSigned(&value))
    Push(static_cast<StackEntry>(value));
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushLEBUnsigned() {
  StackEntry value = 0;
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
    StackEntry top = stack_[stack_.size() - 1];
    StackEntry one_back = stack_[stack_.size() - 2];
    StackEntry two_back = stack_[stack_.size() - 3];

    stack_[stack_.size() - 1] = one_back;
    stack_[stack_.size() - 2] = two_back;
    stack_[stack_.size() - 3] = top;
  }
  return Completion::kSync;
}

// 1 parameter: 2-byte signed constant.
DwarfExprEval::Completion DwarfExprEval::OpSkip() {
  SignedStackEntry skip_amount = 0;
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
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpSwap() {
  if (stack_.size() < 2)
    ReportStackUnderflow();
  else
    std::swap(stack_[stack_.size() - 1], stack_[stack_.size() - 2]);
  return Completion::kSync;
}

void DwarfExprEval::Skip(SignedStackEntry amount) {
  SignedStackEntry new_index = static_cast<SignedStackEntry>(expr_index_) + amount;
  if (new_index >= static_cast<SignedStackEntry>(expr_.size())) {
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
