// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>

#include <limits>
#include <utility>

#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// GNU DWARF operation extensions.
constexpr uint8_t DW_OP_GNU_push_tls_address = 0xe0;
constexpr uint8_t DW_OP_GNU_uninit = 0xf0;
constexpr uint8_t DW_OP_GNU_encoded_addr = 0xf1;
constexpr uint8_t DW_OP_GNU_implicit_pointer = 0xf2;
constexpr uint8_t DW_OP_GNU_entry_value = 0xf3;
constexpr uint8_t DW_OP_GNU_const_type = 0xf4;
constexpr uint8_t DW_OP_GNU_regval_type = 0xf5;
constexpr uint8_t DW_OP_GNU_deref_type = 0xf6;
constexpr uint8_t DW_OP_GNU_convert = 0xf7;
constexpr uint8_t DW_OP_GNU_reinterpret = 0xf9;
constexpr uint8_t DW_OP_GNU_parameter_ref = 0xfa;
constexpr uint8_t DW_OP_GNU_addr_index = 0xfb;
constexpr uint8_t DW_OP_GNU_const_index = 0xfc;
constexpr uint8_t DW_OP_GNU_variable_value = 0xfd;

// For debug printing expression integer values, this uses hex for all but the smallest numbers
// which is what we usually expect for printing address arithmetic.
std::string ExprIntToString(uint128_t v) {
  if (v > 1024)
    return to_hex_string(v);  // Use hex for very large values (probably addresses).
  return to_string(v);
}
std::string ExprIntToString(int128_t v) {
  if (v > 1024)
    return to_hex_string(v);  // Use hex for very large values (probably addresses).
  if (v < -1024)
    return std::string("-") + to_hex_string(-v);
  return to_string(v);
}

// Makes a string expressing adding or subtracting the given constant value.
std::string MakeAddString(DwarfExprEval::SignedType val) {
  if (val < 0)
    return " - " + to_string(-val);
  return " + " + to_string(val);
}

// These macros declare functions to implement arithmetic operators that will work on typed stack
// entries as well as generic values. I suspect this could be written using "template templates" but
// this is mostly boring code used only in this one file and the straightforward #define / custom
// approach is simpler and more maintainable overall.
//
// Before calling these functions, the caller will verify that the types of the two arguments (in
// the case of binary operators) match.
//
// The macros take a validator function which is used for checking for divide-by-zero. In other
// cases, this validator always returns no error.
#define DEFINE_BINARY_OPERATOR(fn_name, bin_operator, validator)                                \
  ErrOr<DwarfStackEntry> fn_name(const DwarfStackEntry& a, const DwarfStackEntry& b) {          \
    if (Err e = validator(a, b); e.has_error())                                                 \
      return e;                                                                                 \
                                                                                                \
    if (a.TreatAsSigned())                                                                      \
      return DwarfStackEntry(a.type_ref(), a.signed_value() bin_operator b.signed_value());     \
    if (a.TreatAsUnsigned())                                                                    \
      return DwarfStackEntry(a.type_ref(), a.unsigned_value() bin_operator b.unsigned_value()); \
    if (a.TreatAsFloat())                                                                       \
      return DwarfStackEntry(a.type_ref(), a.float_value() bin_operator b.float_value());       \
    if (a.TreatAsDouble())                                                                      \
      return DwarfStackEntry(a.type_ref(), a.double_value() bin_operator b.double_value());     \
                                                                                                \
    FX_NOTREACHED();                                                                            \
    return Err("Internal type error.");                                                         \
  }

// Like DEFINE_BINARY_OPERATOR but doesn't support floating point types. Returns an error if the
// input is floating-point
#define DEFINE_INTEGRAL_BINARY_OPERATOR(fn_name, bin_operator, validator)                       \
  ErrOr<DwarfStackEntry> fn_name(const DwarfStackEntry& a, const DwarfStackEntry& b) {          \
    if (Err e = validator(a, b); e.has_error())                                                 \
      return e;                                                                                 \
                                                                                                \
    if (a.TreatAsSigned())                                                                      \
      return DwarfStackEntry(a.type_ref(), a.signed_value() bin_operator b.signed_value());     \
    if (a.TreatAsUnsigned())                                                                    \
      return DwarfStackEntry(a.type_ref(), a.unsigned_value() bin_operator b.unsigned_value()); \
                                                                                                \
    return Err("Requires integral type.");                                                      \
  }

// Like DEFINE_BINARY_OPERATOR but casts the result to a 0 or 1 generic value to implement
// comparison operations.
#define DEFINE_COMPARISON_OPERATOR(fn_name, bin_operator)                              \
  ErrOr<DwarfStackEntry> fn_name(const DwarfStackEntry& a, const DwarfStackEntry& b) { \
    bool result = false;                                                               \
                                                                                       \
    if (a.TreatAsSigned()) {                                                           \
      result = a.signed_value() bin_operator b.signed_value();                         \
    } else if (a.TreatAsUnsigned()) {                                                  \
      result = a.unsigned_value() bin_operator b.unsigned_value();                     \
    } else if (a.TreatAsFloat()) {                                                     \
      result = a.float_value() bin_operator b.float_value();                           \
    } else if (a.TreatAsDouble()) {                                                    \
      result = a.double_value() bin_operator b.double_value();                         \
    } else {                                                                           \
      FX_NOTREACHED();                                                                 \
      return Err("Internal type error.");                                              \
    }                                                                                  \
                                                                                       \
    return DwarfStackEntry(result ? 1 : 0);                                            \
  }

#define DEFINE_INTEGRAL_UNARY_OPERATOR(fn_name, un_operator)                \
  ErrOr<DwarfStackEntry> fn_name(const DwarfStackEntry& a) {                \
    if (a.TreatAsSigned())                                                  \
      return DwarfStackEntry(a.type_ref(), un_operator a.signed_value());   \
    if (a.TreatAsUnsigned())                                                \
      return DwarfStackEntry(a.type_ref(), un_operator a.unsigned_value()); \
                                                                            \
    FX_NOTREACHED();                                                        \
    return Err("Internal type error.");                                     \
  }

Err NoValidator(const DwarfStackEntry&, const DwarfStackEntry& b) { return Err(); }

Err NonzeroDenominatorValidator(const DwarfStackEntry&, const DwarfStackEntry& b) {
  if (b.IsZero())
    return Err("Divide by zero.");
  return Err();
}

// From the DWARF spec:
//
//   Operations other than DW_OP_abs, DW_OP_div, DW_OP_minus, DW_OP_mul, DW_OP_neg and DW_OP_plus
//   require integral types of the operand (either integral base type or the generic type).
//
// We could allow modulo for floating point values also but the spec says not to.
DEFINE_BINARY_OPERATOR(DwarfOperatorPlus, +, NoValidator)
DEFINE_BINARY_OPERATOR(DwarfOperatorMinus, -, NoValidator)
DEFINE_BINARY_OPERATOR(DwarfOperatorTimes, *, NoValidator)
DEFINE_INTEGRAL_BINARY_OPERATOR(DwarfOperatorMod, %, NonzeroDenominatorValidator)

DEFINE_COMPARISON_OPERATOR(DwarfOperatorEquals, ==)
DEFINE_COMPARISON_OPERATOR(DwarfOperatorNotEquals, !=)
DEFINE_COMPARISON_OPERATOR(DwarfOperatorLessEquals, <=)
DEFINE_COMPARISON_OPERATOR(DwarfOperatorGreaterEquals, >=)
DEFINE_COMPARISON_OPERATOR(DwarfOperatorLess, <)
DEFINE_COMPARISON_OPERATOR(DwarfOperatorGreater, >)

DEFINE_INTEGRAL_BINARY_OPERATOR(DwarfOperatorShiftLeft, <<, NoValidator)
DEFINE_INTEGRAL_BINARY_OPERATOR(DwarfOperatorShiftRight, >>, NoValidator)
DEFINE_INTEGRAL_BINARY_OPERATOR(DwarfOperatorBitOr, |, NoValidator)
DEFINE_INTEGRAL_BINARY_OPERATOR(DwarfOperatorBitAnd, &, NoValidator)
DEFINE_INTEGRAL_BINARY_OPERATOR(DwarfOperatorBitXor, ^, NoValidator)

DEFINE_INTEGRAL_UNARY_OPERATOR(DwarfOperatorBitNot, ~)

// The "DW_OP_div" specifies signed division in the generic type case (actually it specifies signed
// division for all cases, but if the values are typed unsigned, we do unsigned division because it
// seems to make more sense). This is implemented using the macro for the common cases, but a
// custom wrapper implementation that catches the generic case.
DEFINE_BINARY_OPERATOR(DwarfOperatorDivideTyped, /, NonzeroDenominatorValidator)
ErrOr<DwarfStackEntry> DwarfOperatorDivide(const DwarfStackEntry& a, const DwarfStackEntry& b) {
  if (a.is_generic()) {
    if (Err e = NonzeroDenominatorValidator(a, b); e.has_error())
      return e;
    return DwarfStackEntry(static_cast<DwarfStackEntry::SignedType>(a.unsigned_value()) /
                           static_cast<DwarfStackEntry::SignedType>(b.unsigned_value()));
  }
  return DwarfOperatorDivideTyped(a, b);
}

// Negation requires that "generic" values are casted to signed.
//
// There is a question on how to define negation of an explicitly unsigned value. The spec doesn't
// say anything. This currently defines it as a no-op, but it could also be like the generic case
// where the bits are reinterpreted. Probably this will not be emitted by a compiler so it won't
// matter.
ErrOr<DwarfStackEntry> DwarfOperatorNeg(const DwarfStackEntry& a) {
  if (a.is_generic())
    return DwarfStackEntry(static_cast<uint128_t>(-static_cast<int128_t>(a.unsigned_value())));

  if (a.TreatAsSigned())
    return DwarfStackEntry(a.type_ref(), -a.signed_value());
  if (a.TreatAsUnsigned())
    return DwarfStackEntry(a.type_ref(), a.unsigned_value());  // No-op, see above.
  if (a.TreatAsFloat())
    return DwarfStackEntry(a.type_ref(), -a.float_value());
  if (a.TreatAsDouble())
    return DwarfStackEntry(a.type_ref(), -a.double_value());

  FX_NOTREACHED();
  return Err("Internal type error");
}

// This one is an operator to DWARF but not C++. Generic values get treated as signed.
ErrOr<DwarfStackEntry> DwarfOperatorAbs(const DwarfStackEntry& a) {
  if (a.is_generic()) {
    auto as_signed = static_cast<DwarfStackEntry::SignedType>(a.unsigned_value());
    return as_signed < 0 ? DwarfStackEntry(static_cast<DwarfStackEntry::UnsignedType>(-as_signed))
                         : a;
  }

  // Explicit types. The DWARF spec doesn't separate out non-generic types so technically the
  // requirement that the result is "interpreted as signed" applies, but I don't think they meant
  // that if the value has an explicitly defined unsigned type.
  if (a.TreatAsSigned()) {
    if (a.signed_value() < 0)
      return DwarfStackEntry(a.type_ref(), -a.signed_value());
    return DwarfStackEntry(a.type_ref(), a.signed_value());
  }
  if (a.TreatAsUnsigned())
    return DwarfStackEntry(a.type_ref(), a.unsigned_value());  // No-op.
  if (a.TreatAsFloat())
    return DwarfStackEntry(a.type_ref(), fabsf(a.float_value()));
  if (a.TreatAsDouble())
    return DwarfStackEntry(a.type_ref(), fabs(a.double_value()));

  FX_NOTREACHED();
  return Err("Internal type error");
}

// This does a sign-extended shift right. When the type is known, we do the same thing as a regular
// shift right (sign-extended only for signed types). I'm assuming that the DWARF spec's
// description of reinterpreting as a signed type only applies to generic types.
ErrOr<DwarfStackEntry> DwarfOperatorShiftRightArithmetically(const DwarfStackEntry& a,
                                                             const DwarfStackEntry& b) {
  if (a.is_generic()) {
    return DwarfStackEntry(static_cast<uint128_t>(static_cast<int128_t>(a.unsigned_value()) >>
                                                  static_cast<int128_t>(b.unsigned_value())));
  }
  return DwarfOperatorShiftRight(a, b);
}

}  // namespace

DwarfExprEval::DwarfExprEval(UnitSymbolFactory symbol_factory,
                             fxl::RefPtr<SymbolDataProvider> data_provider,
                             const SymbolContext& symbol_context)
    : unit_symbol_factory_(std::move(symbol_factory)),
      data_provider_(std::move(data_provider)),
      symbol_context_(symbol_context),
      weak_factory_(this) {
  FX_DCHECK(data_provider_);
}

DwarfExprEval::~DwarfExprEval() {
  // This assertion verifies that this class was not accidentally deleted from
  // within the completion callback. This class is not set up to handle this
  // case.
  FX_CHECK(!in_completion_callback_);
}

void DwarfExprEval::Push(DwarfStackEntry value) { stack_.push_back(value); }

DwarfExprEval::ResultType DwarfExprEval::GetResultType() const {
  FX_DCHECK(is_complete_);
  FX_DCHECK(is_success_);

  if (!result_data_.empty())
    return ResultType::kData;
  return result_type_;
}

DwarfStackEntry DwarfExprEval::GetResult() const {
  FX_DCHECK(is_complete_);
  FX_DCHECK(is_success_);
  return stack_.back();
}

TaggedData DwarfExprEval::TakeResultData() {
  FX_DCHECK(is_complete_);
  FX_DCHECK(GetResultType() == ResultType::kData);
  return result_data_.TakeData();
}

DwarfExprEval::Completion DwarfExprEval::Eval(DwarfExpr expr, CompletionCallback cb) {
  SetUp(expr, std::move(cb));

  // Note: ContinueEval() may call callback, which may delete |this|.
  return ContinueEval() ? Completion::kSync : Completion::kAsync;
}

std::string DwarfExprEval::ToString(DwarfExpr expr, bool pretty) {
  SetUp(expr, nullptr);
  result_data_ = TaggedDataBuilder();

  string_output_mode_ = pretty ? StringOutput::kPretty : StringOutput::kLiteral;
  string_output_.clear();

  bool is_complete = ContinueEval();
  FX_DCHECK(is_complete);  // Always expect string printing mode to complete.

  std::string result = std::move(string_output_);
  string_output_mode_ = StringOutput::kNone;
  string_output_.clear();

  return result;
}

void DwarfExprEval::SetUp(DwarfExpr expr, CompletionCallback cb) {
  is_complete_ = false;
  expr_ = std::move(expr);
  completion_callback_ = std::move(cb);
  data_extractor_ = DataExtractor(expr_.data());
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
    if (!is_complete_ && data_extractor_.done()) {
      if (is_string_output())
        return true;  // Only expecting to produce a string.

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
      debug::MessageLoop::Current()->PostTask(FROM_HERE,
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
  FX_DCHECK(!is_complete_);
  FX_DCHECK(!data_extractor_.done());

  // Opcode is next byte in the data buffer. Consume it (we already checked there's data).
  uint8_t op = *data_extractor_.Read<uint8_t>();

  // Clear any current register information whenever the stack is being mutated.
  // See current_register_id_ declaration for more.
  if (op != llvm::dwarf::DW_OP_stack_value)
    current_register_id_ = debug::RegisterID::kUnknown;

  // Literals 0-31.
  if (op >= llvm::dwarf::DW_OP_lit0 && op <= llvm::dwarf::DW_OP_lit31) {
    int literal_value = op - llvm::dwarf::DW_OP_lit0;
    if (is_string_output()) {
      return AppendString("DW_OP_lit" + std::to_string(literal_value),
                          "push(" + std::to_string(literal_value) + ")");
    } else {
      Push(DwarfStackEntry(literal_value));
    }
    return Completion::kSync;
  }

  // Registers 0-31.
  if (op >= llvm::dwarf::DW_OP_reg0 && op <= llvm::dwarf::DW_OP_reg31) {
    int reg_index = op - llvm::dwarf::DW_OP_reg0;
    if (is_string_output())
      return AppendString("DW_OP_reg" + std::to_string(reg_index), GetRegisterName(reg_index));

    result_type_ = ResultType::kValue;
    return PushRegisterWithOffset(reg_index, 0);
  }

  // Base register with SLEB128 offset.
  if (op >= llvm::dwarf::DW_OP_breg0 && op <= llvm::dwarf::DW_OP_breg31)
    return OpBreg(op);

  switch (op) {
    case llvm::dwarf::DW_OP_addr:
      return OpAddr();
    case llvm::dwarf::DW_OP_deref:
      return OpDeref(sizeof(TargetPointer), nullptr, "DW_OP_deref", false);
    case llvm::dwarf::DW_OP_const1u:
      return OpPushUnsigned(1, "DW_OP_const1u");
    case llvm::dwarf::DW_OP_const1s:
      return OpPushSigned(1, "DW_OP_const1s");
    case llvm::dwarf::DW_OP_const2u:
      return OpPushUnsigned(2, "DW_OP_const2u");
    case llvm::dwarf::DW_OP_const2s:
      return OpPushSigned(2, "DW_OP_const2s");
    case llvm::dwarf::DW_OP_const4u:
      return OpPushUnsigned(4, "DW_OP_const4u");
    case llvm::dwarf::DW_OP_const4s:
      return OpPushSigned(4, "DW_OP_const4s");
    case llvm::dwarf::DW_OP_const8u:
      return OpPushUnsigned(8, "DW_OP_const8u");
    case llvm::dwarf::DW_OP_const8s:
      return OpPushSigned(8, "DW_OP_const8s");
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
      return ReportError("DW_OP_xderef opcode is not applicable to this platform.");
    case llvm::dwarf::DW_OP_abs:
      return OpUnary(&DwarfOperatorAbs, "DW_OP_abs");
    case llvm::dwarf::DW_OP_and:
      return OpBinary(&DwarfOperatorBitAnd, "DW_OP_and");
    case llvm::dwarf::DW_OP_div:
      return OpBinary(&DwarfOperatorDivide, "DW_OP_div");
    case llvm::dwarf::DW_OP_minus:
      return OpBinary(&DwarfOperatorMinus, "DW_OP_minus");
    case llvm::dwarf::DW_OP_mod:
      return OpBinary(&DwarfOperatorMod, "DW_OP_mod");
    case llvm::dwarf::DW_OP_mul:
      return OpBinary(&DwarfOperatorTimes, "DW_OP_mul");
    case llvm::dwarf::DW_OP_neg:
      return OpUnary(&DwarfOperatorNeg, "DW_OP_neg");
    case llvm::dwarf::DW_OP_not:
      return OpUnary(&DwarfOperatorBitNot, "DW_OP_not");
    case llvm::dwarf::DW_OP_or:
      return OpBinary(&DwarfOperatorBitOr, "DW_OP_or");
    case llvm::dwarf::DW_OP_plus:
      return OpBinary(&DwarfOperatorPlus, "DW_OP_plus");
    case llvm::dwarf::DW_OP_plus_uconst:
      return OpPlusUconst();
    case llvm::dwarf::DW_OP_shl:
      return OpBinary(&DwarfOperatorShiftLeft, "DW_OP_shl");
    case llvm::dwarf::DW_OP_shr:
      return OpBinary(&DwarfOperatorShiftRight, "DW_OP_shr");
    case llvm::dwarf::DW_OP_shra:
      return OpBinary(&DwarfOperatorShiftRightArithmetically, "DW_OP_shra");
    case llvm::dwarf::DW_OP_xor:
      return OpBinary(&DwarfOperatorBitXor, "DW_OP_xor");
    case llvm::dwarf::DW_OP_bra:
      return OpBra();
    case llvm::dwarf::DW_OP_eq:
      return OpBinary(&DwarfOperatorEquals, "DW_OP_eq");
    case llvm::dwarf::DW_OP_ge:
      return OpBinary(&DwarfOperatorGreaterEquals, "DW_OP_ge");
    case llvm::dwarf::DW_OP_gt:
      return OpBinary(&DwarfOperatorGreater, "DW_OP_gt");
    case llvm::dwarf::DW_OP_le:
      return OpBinary(&DwarfOperatorLessEquals, "DW_OP_le");
    case llvm::dwarf::DW_OP_lt:
      return OpBinary(&DwarfOperatorLess, "DW_OP_lt");
    case llvm::dwarf::DW_OP_ne:
      return OpBinary(&DwarfOperatorNotEquals, "DW_OP_ne");
    case llvm::dwarf::DW_OP_skip:
      return OpSkip();
      // DW_OP_lit*, DW_OP_reg*, and DW_OP_breg* are handled at the top of the function.
    case llvm::dwarf::DW_OP_regx:
      return OpRegx();
    case llvm::dwarf::DW_OP_fbreg:
      return OpFbreg();
    case llvm::dwarf::DW_OP_bregx:
      return OpBregx();
    case llvm::dwarf::DW_OP_piece:
      return OpPiece();
    case llvm::dwarf::DW_OP_deref_size:
      return OpDerefSize();
    case llvm::dwarf::DW_OP_xderef_size:
      // We don't have multiple address spaces.
      return ReportError("DW_OP_xderef_size opcode is not applicable to this platform.");
    case llvm::dwarf::DW_OP_nop:
      if (is_string_output())
        AppendString("DW_OP_nop");
      return Completion::kSync;

    // DWARF 3 additions.
    case llvm::dwarf::DW_OP_push_object_address:
      return ReportError("Unimplemented DW_OP_push_object_address opcode.");
    case llvm::dwarf::DW_OP_call2:     // 2-byte offset of DIE.
    case llvm::dwarf::DW_OP_call4:     // 4-byte offset of DIE.
    case llvm::dwarf::DW_OP_call_ref:  // 4- or 8-byte offset of DIE.
      return ReportError("Unimplemented DWARF expression procedure call operation.");
    case llvm::dwarf::DW_OP_form_tls_address:
      return OpTlsAddr("DW_OP_form_tls_address");
    case llvm::dwarf::DW_OP_call_frame_cfa:
      return OpCFA();
    case llvm::dwarf::DW_OP_bit_piece:
      return OpBitPiece();

    // DWARF 4 additions.
    case llvm::dwarf::DW_OP_implicit_value:
      return OpImplicitValue();
    case llvm::dwarf::DW_OP_stack_value:
      return OpStackValue();

    // DWARF 5 additions.
    case llvm::dwarf::DW_OP_implicit_pointer:
      return OpImplicitPointer("DW_OP_implicit_pointer");
    case llvm::dwarf::DW_OP_addrx:
      return OpAddrBase(ResultType::kPointer, "DW_OP_addrx");
    case llvm::dwarf::DW_OP_constx:
      return OpAddrBase(ResultType::kValue, "DW_OP_constx");
    case llvm::dwarf::DW_OP_entry_value:
      return OpEntryValue("DW_OP_entry_value");
    case llvm::dwarf::DW_OP_const_type:
      return OpConstType("DW_OP_const_type");
    case llvm::dwarf::DW_OP_regval_type:
      return OpRegvalType("DW_OP_regval_type");
    case llvm::dwarf::DW_OP_deref_type:
      return OpDerefType("DW_OP_deref_type");
    case llvm::dwarf::DW_OP_xderef_type:
      // We don't have multiple address spaces.
      return ReportError("DW_OP_xderef_type opcode is not applicable to this platform.");
    case llvm::dwarf::DW_OP_convert:
      return OpConvert("DW_OP_convert");
    case llvm::dwarf::DW_OP_reinterpret:
      return OpReinterpret("DW_OP_reinterpret");

    // GNU extensions.
    case DW_OP_GNU_push_tls_address:
      return OpTlsAddr("DW_OP_GNU_push_tls_address");
    case DW_OP_GNU_uninit:
      // Reports that the variable is uninitialized (as opposed to optimized out).
      return ReportError("Unimplemented DW_OP_GNU_uninit opcode.");
    case DW_OP_GNU_encoded_addr:
      return ReportError("Unimplemented DW_OP_GNU_encoded_addr opcode.");
    case DW_OP_GNU_implicit_pointer:
      return OpImplicitPointer("DW_OP_GNU_implicit_pointer");
    case DW_OP_GNU_entry_value:
      return OpEntryValue("DW_OP_GNU_entry_value");
    case DW_OP_GNU_const_type:
      return OpConstType("DW_OP_GNU_const_type");
    case DW_OP_GNU_regval_type:
      return OpRegvalType("DW_OP_GNU_regval_type");
    case DW_OP_GNU_deref_type:
      return OpDerefType("DW_OP_GNU_deref_type");
    case DW_OP_GNU_convert:
      return OpConvert("DW_OP_GNU_convert");
    case DW_OP_GNU_reinterpret:
      return OpReinterpret("DW_OP_GNU_reinterpret");
    case DW_OP_GNU_parameter_ref:
      return ReportError("Unimplemented DW_OP_GNU_parameter_ref opcode.");
    case DW_OP_GNU_addr_index:
      return ReportError("Unimplemented DW_OP_GNU_addr_index opcode.");
    case DW_OP_GNU_const_index:
      return ReportError("Unimplemented DW_OP_GNU_const_index opcode.");
    case DW_OP_GNU_variable_value:
      return ReportError("Unimplemented DW_OP_GNU_variable_value opcode.");

    default:
      // Invalid or unknown opcode.
      if (is_string_output()) {
        AppendString("INVALID_OPCODE(" + to_hex_string(op) + ")");
      } else {
        ReportError(fxl::StringPrintf("Invalid opcode 0x%x in DWARF expression.", op));
      }
      return Completion::kSync;
  }
}

DwarfExprEval::Completion DwarfExprEval::GetRegisterValue(
    int dwarf_register_number,
    fit::function<void(DwarfExprEval* eval, Completion completion,
                       std::optional<UnsignedType> value, const debug::RegisterInfo* info)>
        cb) {
  const debug::RegisterInfo* reg_info =
      debug::DWARFToRegisterInfo(data_provider_->GetArch(), dwarf_register_number);
  if (!reg_info) {
    ReportError(fxl::StringPrintf("DWARF register %d not known.", dwarf_register_number));
    cb(this, Completion::kSync, std::nullopt, nullptr);
    return Completion::kSync;
  }

  if (auto reg_data = data_provider_->GetRegister(reg_info->id)) {
    // State known synchronously (could be available or known unavailable).
    if (reg_data->empty()) {
      ReportError(fxl::StringPrintf("Register %s not available.", reg_info->name.c_str()));
      cb(this, Completion::kSync, std::nullopt, nullptr);
    } else {
      // This truncates to 128 bits and converts from little-endian. DWARF doesn't seem to use the
      // stack machine for vector computations (it's not specified that the stack items are large
      // enough). When it uses a stack register for a floating-point scalar computation, it just
      // uses the low bits.
      UnsignedType reg_value = 0;
      memcpy(&reg_value, reg_data->data(), std::min(sizeof(UnsignedType), reg_data->size()));
      cb(this, Completion::kSync, reg_value, reg_info);
    }
    return Completion::kSync;
  }

  // Must request async. The reg_info is static data so the pointer can be captured.
  data_provider_->GetRegisterAsync(
      reg_info->id, [weak_eval = weak_factory_.GetWeakPtr(), reg_info, cb = std::move(cb)](
                        const Err& err, std::vector<uint8_t> reg_data) mutable {
        if (!weak_eval)
          return;
        if (err.has_error()) {
          weak_eval->ReportError(err);
          cb(weak_eval.get(), Completion::kAsync, std::nullopt, nullptr);
          return;
        }

        // Truncate/convert from little-endian as above.
        UnsignedType reg_value = 0;
        memcpy(&reg_value, reg_data.data(), std::min(sizeof(UnsignedType), reg_data.size()));
        cb(weak_eval.get(), Completion::kAsync, reg_value, reg_info);
      });

  return Completion::kAsync;
}

DwarfExprEval::Completion DwarfExprEval::PushRegisterWithOffset(int dwarf_register_number,
                                                                SignedType offset) {
  return GetRegisterValue(
      dwarf_register_number,
      [offset](DwarfExprEval* eval, Completion completion, std::optional<UnsignedType> value,
               const debug::RegisterInfo* info) {
        if (value) {
          // Success.
          eval->Push(DwarfStackEntry(*value + offset));

          // Reading register data means the result is not constant.
          eval->result_is_constant_ = false;

          // When the current value represents a literal register value, save that fact.
          if (offset == 0)
            eval->current_register_id_ = info->id;

          if (completion == Completion::kAsync)
            eval->ContinueEval();  // Pick up processing at the next instruction.
        }
      });
}

bool DwarfExprEval::ReadSigned(int byte_size, SignedType* output) {
  switch (byte_size) {
    case 1:
      if (auto v = data_extractor_.Read<int8_t>()) {
        *output = *v;
        return true;
      }
      break;
    case 2:
      if (auto v = data_extractor_.Read<int16_t>()) {
        *output = *v;
        return true;
      }
      break;
    case 4:
      if (auto v = data_extractor_.Read<int32_t>()) {
        *output = *v;
        return true;
      }
      break;
    case 8:
      if (auto v = data_extractor_.Read<int64_t>()) {
        *output = *v;
        return true;
      }
      break;
  }

  ReportError("Bad number format in DWARF expression.");
  return false;
}

bool DwarfExprEval::ReadUnsigned(int byte_size, UnsignedType* output) {
  switch (byte_size) {
    case 1:
      if (auto v = data_extractor_.Read<uint8_t>()) {
        *output = *v;
        return true;
      }
      break;
    case 2:
      if (auto v = data_extractor_.Read<uint16_t>()) {
        *output = *v;
        return true;
      }
      break;
    case 4:
      if (auto v = data_extractor_.Read<uint32_t>()) {
        *output = *v;
        return true;
      }
      break;
    case 8:
      if (auto v = data_extractor_.Read<uint64_t>()) {
        *output = *v;
        return true;
      }
      break;
  }

  ReportError("Bad number format in DWARF expression.");
  return false;
}

bool DwarfExprEval::ReadLEBSigned(SignedType* output) {
  if (auto result = data_extractor_.ReadSleb128()) {
    *output = *result;
    return true;
  }
  ReportError("Bad number format in DWARF expression.");
  return false;
}

bool DwarfExprEval::ReadLEBUnsigned(UnsignedType* output) {
  if (auto result = data_extractor_.ReadUleb128()) {
    *output = *result;
    return true;
  }
  ReportError("Bad number format in DWARF expression.");
  return false;
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

DwarfExprEval::Completion DwarfExprEval::ReportError(const std::string& msg) {
  return ReportError(Err(msg));
}

DwarfExprEval::Completion DwarfExprEval::ReportError(const Err& err) {
  if (is_string_output())
    AppendString("ERROR: \"" + err.msg() + "\"");

  is_complete_ = true;

  // Wrap completion callback with the flag to catch deletions from within the callback.
  in_completion_callback_ = true;
  if (completion_callback_)
    completion_callback_(this, err);
  completion_callback_ = {};
  in_completion_callback_ = false;

  return Completion::kSync;
}

void DwarfExprEval::ReportStackUnderflow() { ReportError("Stack underflow for DWARF expression."); }

DwarfExprEval::Completion DwarfExprEval::OpUnary(
    ErrOr<DwarfStackEntry> (*op)(const DwarfStackEntry&), const char* op_name) {
  if (is_string_output())
    return AppendString(op_name);

  if (stack_.empty()) {
    ReportStackUnderflow();
  } else {
    ErrOr<DwarfStackEntry> result = op(stack_.back());
    if (result.ok()) {
      stack_.back() = std::move(result.value());
    } else {
      ReportError("Error evaluating " + std::string(op_name) +
                  " in DWARF expression: " + result.err().msg());
    }
  }
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpBinary(
    ErrOr<DwarfStackEntry> (*op)(const DwarfStackEntry&, const DwarfStackEntry&),
    const char* op_name) {
  if (is_string_output())
    return AppendString(op_name);

  if (stack_.size() < 2) {
    ReportStackUnderflow();
  } else {
    DwarfStackEntry b = stack_.back();
    stack_.pop_back();
    DwarfStackEntry a = stack_.back();
    stack_.pop_back();

    if (!a.SameTypeAs(b)) {
      ReportError(std::string(op_name) + " called on incompatible types " + a.GetTypeDescription() +
                  " and " + b.GetTypeDescription());
      return Completion::kSync;
    }

    ErrOr<DwarfStackEntry> result = op(a, b);
    if (result.ok()) {
      stack_.push_back(std::move(result.value()));
    } else {
      ReportError("Error evaluating " + std::string(op_name) +
                  " in DWARF expression: " + result.err().msg());
    }
  }
  return Completion::kSync;
}

// ULEB128 index into the .debug_addr section where a machine address-length value is stored. The
// index is relative to the value of the DW_AT_addr_base attribute of the compilation unit.
//
// result_type == kAddress corresponds to DW_OP_addrx
// result_type == kValue corresponds to DW_OP_constx.
DwarfExprEval::Completion DwarfExprEval::OpAddrBase(ResultType result_type, const char* op_name) {
  fxl::WeakPtr<ModuleSymbols> module_symbols = expr_.source().Get()->GetModuleSymbols();
  if (!module_symbols)
    return ReportError(std::string("DWARF expression used ") + op_name +
                       "but no symbols are available.");

  // The index in the expression is inside the table identified by the DW_AT_addr_base of the
  // compilation unit.
  std::optional<uint64_t> base = expr_.GetAddrBase();
  if (!base) {
    return ReportError(std::string("DWARF expression used ") + op_name +
                       " but no addr_base is available.");
  }

  UnsignedType addr_table_index;
  if (!ReadLEBUnsigned(&addr_table_index))
    return Completion::kSync;

  std::optional<uint64_t> result_or =
      module_symbols->GetDebugAddrEntry(*base, static_cast<uint64_t>(addr_table_index));
  if (!result_or)
    return ReportError("Unable to read .debug_addr section to evaluate expression.");

  result_type_ = result_type;
  if (result_type == ResultType::kPointer) {
    // Addresses need to be relocated according to the module offset.
    UnsignedType new_entry = symbol_context_.RelativeToAbsolute(result_or.value());
    if (is_string_output()) {
      AppendString(std::string(op_name) + "(" + ExprIntToString(addr_table_index) +
                   ", with addr_base=" + to_hex_string(*base) + ") -> rel=" +
                   to_hex_string(result_or.value()) + ", abs=" + to_hex_string(new_entry));
    } else {
      Push(DwarfStackEntry(new_entry));
    }
  } else {
    // Constants are ready to use.
    UnsignedType new_entry = static_cast<UnsignedType>(result_or.value());
    if (is_string_output()) {
      AppendString(std::string(op_name) + "(" + ExprIntToString(addr_table_index) +
                   ", with addr_base=" + to_hex_string(*base) + ") -> " + to_hex_string(new_entry));
    } else {
      Push(DwarfStackEntry(new_entry));
    }
  }

  return Completion::kSync;
}

// 1 parameter: unsigned the size of a pointer. This is relative to the load address of the current
// module. It is used to for globals and statics.
DwarfExprEval::Completion DwarfExprEval::OpAddr() {
  UnsignedType offset;
  if (!ReadUnsigned(kTargetPointerSize, &offset))
    return Completion::kSync;

  TargetPointer address = symbol_context_.RelativeToAbsolute(static_cast<TargetPointer>(offset));

  if (is_string_output()) {
    if (symbol_context_.is_relative() || string_output_mode_ == StringOutput::kLiteral)
      return AppendString("DW_OP_addr(" + to_hex_string(offset) + ")");

    // Show final address since we know it.
    return AppendString("push(" + to_hex_string(address) + ")");
  }

  Push(DwarfStackEntry(address));
  return Completion::kSync;
}

// ULEB128 size + ULEB128 offset.
DwarfExprEval::Completion DwarfExprEval::OpBitPiece() {
  UnsignedType size;
  if (!ReadLEBUnsigned(&size))
    return Completion::kSync;

  UnsignedType offset;
  if (!ReadLEBUnsigned(&offset))
    return Completion::kSync;

  if (is_string_output())
    return AppendString("DW_OP_bit_piece(" + ExprIntToString(size) + ", " +
                        ExprIntToString(offset) + ")");

  // Clang will generate bit_piece operations to make 80-bit long double constants, but the
  // expressions are invalid: https://bugs.llvm.org/show_bug.cgi?id=43682
  // We were able to get GCC to generate a piece operation for:
  //   void foo(int x, int y) {
  //     struct { int x:3, :3, y:3; } s = {x, y};
  //   }
  // That also seems invalid. So we're waiting for a clearly valid example in the wild before
  // spending time trying to implement this.
  return ReportError(
      "The DWARF encoding for this symbol uses DW_OP_bit_piece which is unimplemented.\n"
      "Please file a bit with a repro case so we can implement it properly.");
}

// 1 parameter: 2 byte signed integer constant.
DwarfExprEval::Completion DwarfExprEval::OpBra() {
  // "The 2-byte constant is the number of bytes of the DWARF expression to skip forward or backward
  // from the current operation, beginning after the 2-byte constant."
  SignedType skip_amount = 0;
  if (!ReadSigned(2, &skip_amount))
    return Completion::kSync;

  if (is_string_output())
    return AppendString("DW_OP_bra(" + ExprIntToString(skip_amount) + ")");

  if (stack_.empty()) {
    ReportStackUnderflow();
    return Completion::kSync;
  }

  // 0 @ top of stack means don't take the branch.
  DwarfStackEntry condition = stack_.back();
  stack_.pop_back();
  if (condition.IsZero())
    return Completion::kSync;

  // Otherwise take the branch.
  Skip(skip_amount);
  return Completion::kSync;
}

// 1 parameter: SLEB128 offset added to base register.
DwarfExprEval::Completion DwarfExprEval::OpBreg(uint8_t op) {
  int reg_index = op - llvm::dwarf::DW_OP_breg0;

  SignedType offset = 0;
  if (!ReadLEBSigned(&offset))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString(
        "DW_OP_breg" + std::to_string(reg_index) + "(" + ExprIntToString(offset) + ")",
        GetRegisterName(reg_index) + MakeAddString(offset));
  }

  result_type_ = ResultType::kPointer;
  return PushRegisterWithOffset(reg_index, offset);
}

DwarfExprEval::Completion DwarfExprEval::OpCFA() {
  if (is_string_output())
    return AppendString("DW_OP_call_frame_cfa");

  // Reading the CFA means the result is not constant.
  result_is_constant_ = false;

  if (UnsignedType cfa = data_provider_->GetCanonicalFrameAddress())
    Push(DwarfStackEntry(cfa));
  else
    ReportError("Frame address is 0.");
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpDrop() {
  if (is_string_output())
    return AppendString("DW_OP_drop");

  if (stack_.empty())
    ReportStackUnderflow();
  else
    stack_.pop_back();
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpDup() {
  if (is_string_output())
    return AppendString("DW_OP_dup");

  if (stack_.empty())
    ReportStackUnderflow();
  else
    stack_.push_back(stack_.back());
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpEntryValue(const char* op_name) {
  // A ULEB128 length followed by a sub-expression of that length. This sub-expression is evaluated
  // in a separate stack using the register values that were present at the beginning of the
  // function.
  UnsignedType length;
  if (!ReadLEBUnsigned(&length))
    return Completion::kSync;
  if (length == 0 || !data_extractor_.CanRead(static_cast<size_t>(length)))
    return ReportError("DW_OP_entry_value sub expression is a bad length.");

  // Read the DWARF expression to evaluate.
  std::vector<uint8_t> sub_expr_bytes(static_cast<size_t>(length));
  if (!data_extractor_.ReadBytes(sub_expr_bytes.size(), sub_expr_bytes.data()))
    return ReportError("Not enough data for DW_OP_entry_value.");

  // Get the data provider for the nested context.
  auto entry_data_provider = data_provider_->GetEntryDataProvider();
  if (!entry_data_provider) {
    if (is_string_output()) {
      // When doing string output, we don't actually need the entry data, we just need to be able
      // to print the data. This needs to work to be able to print symbol debug information that
      // might not be active. In this case, just use the same data provider for the entry data, it
      // should be unused.
      entry_data_provider = data_provider_;
    } else {
      return ReportError("Can not compute function entry values in this context.");
    }
  }

  FX_DCHECK(!nested_eval_);
  nested_eval_ = std::make_unique<DwarfExprEval>(unit_symbol_factory_,
                                                 std::move(entry_data_provider), symbol_context_);

  DwarfExpr sub_expr(std::move(sub_expr_bytes), expr_.source());

  if (is_string_output()) {
    std::string nested_str(op_name);
    nested_str += "(";

    // For string output the data provider doesn't matter, so keep using ours since the entry
    // provider might not be available.
    nested_str +=
        nested_eval_->ToString(std::move(sub_expr), string_output_mode_ == StringOutput::kPretty);

    nested_str += ")";
    AppendString(nested_str);
    nested_eval_.reset();
    return Completion::kSync;
  }

  // Since we own the nested evaluator, it's OK to capture |this| in the callback.
  //
  // The nested evaluator may complete synchronously (the common case) or not. We need to know that
  // from the callback to know whether to call ContinueEval() or not, and this information isn't
  // passed to the callback. The shared boolean allows us to track this.
  auto is_async_completion = std::make_shared<bool>(false);
  Completion completion = nested_eval_->Eval(
      std::move(sub_expr), [this, is_async_completion](DwarfExprEval* nested, const Err& err) {
        // TODO(brettw) do we need to call ContinueEval on error? What about in other cases this
        // comes up? They may be wrong.
        if (err.has_error()) {
          ReportError(err);
        } else if (nested->GetResultType() == ResultType::kData) {
          // The nested expression is expected to produce exactly one stack entry, not data.
          ReportError("DWARF entry value expression produced an incorrect result type.");
        } else {
          // Success case, save the result.
          Push(DwarfStackEntry(nested->GetResult()));
        }

        if (*is_async_completion) {
          // The nested_eval_ needs to be cleared before continuing, but we can't delete it from
          // within its own callback. Schedule it to be deleted from the message loop.
          debug::MessageLoop::Current()->PostTask(FROM_HERE,
                                                  [old_eval = std::move(nested_eval_)]() {});
          ContinueEval();
        }
      });

  if (completion == Completion::kSync) {
    nested_eval_.reset();
  } else {
    *is_async_completion = true;
  }
  return completion;
}

// 1 parameter: Signed LEB128 offset from frame base pointer.
DwarfExprEval::Completion DwarfExprEval::OpFbreg() {
  // Reading the frame base means the result is not constant.
  result_is_constant_ = false;

  SignedType offset = 0;
  if (!ReadLEBSigned(&offset))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString("DW_OP_fbreg(" + ExprIntToString(offset) + ")",
                        "frame_base" + MakeAddString(offset));
  }

  if (auto bp = data_provider_->GetFrameBase()) {
    // Available synchronously.

    // Certain problems can cause the BP to be set to 0 which is obviously
    // invalid, report that error specifically.
    if (*bp == 0)
      return ReportError("Base Pointer is 0, can't evaluate.");

    result_type_ = ResultType::kPointer;
    Push(DwarfStackEntry(*bp + offset));
    return Completion::kSync;
  }

  // Must request async.
  data_provider_->GetFrameBaseAsync(
      [weak_eval = weak_factory_.GetWeakPtr(), offset](const Err& err, UnsignedType value) {
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
        weak_eval->Push(DwarfStackEntry(static_cast<UnsignedType>(value + offset)));

        // Picks up processing at the next instruction.
        weak_eval->ContinueEval();
      });

  return Completion::kAsync;
}

// 2 parameters: 8-byte unsigned DIE offset containing the value, SLEB128 offset from that value.
DwarfExprEval::Completion DwarfExprEval::OpImplicitPointer(const char* op_name) {
  // GCC generates this when a pointer has been optimized out, but it still can provide the value of
  // the thing that it pointed to. We don't implement this.
  UnsignedType die_offset;
  if (!ReadUnsigned(8, &die_offset))
    return Completion::kSync;

  SignedType value_offset;
  if (!ReadLEBSigned(&value_offset))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString(std::string(op_name) + "(" + to_hex_string(die_offset) + ", " +
                        ExprIntToString(value_offset) + ")");
  }

  return ReportError("Optimized out (DW_OP_implicit_pointer)");
}

// 2 parameters: ULEB128 length, followed by that much data (in machine-endianness).
DwarfExprEval::Completion DwarfExprEval::OpImplicitValue() {
  UnsignedType len = 0;
  if (!ReadLEBUnsigned(&len))
    return Completion::kSync;
  if (len > sizeof(UnsignedType))
    return ReportError(fxl::StringPrintf("DWARF implicit value length too long: 0x%x.",
                                         static_cast<unsigned>(len)));

  UnsignedType value = 0;
  if (!data_extractor_.ReadBytes(static_cast<size_t>(len), &value))
    return ReportError("Not enough data for DWARF implicit value.");

  if (is_string_output()) {
    return AppendString(
        "DW_OP_implicit_value(" + ExprIntToString(len) + ", " + to_hex_string(value) + ")",
        "push(" + to_hex_string(value) + ")");
  }

  Push(DwarfStackEntry(value));
  result_type_ = ResultType::kValue;
  return Completion::kSync;
}

// 1 parameter: ULEB128 constant indexing the register.
DwarfExprEval::Completion DwarfExprEval::OpRegx() {
  UnsignedType reg = 0;
  if (!ReadLEBUnsigned(&reg))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString("DW_OP_regx(" + ExprIntToString(reg) + ")",
                        GetRegisterName(static_cast<int>(reg)));
  }

  result_type_ = ResultType::kValue;
  return PushRegisterWithOffset(static_cast<int>(reg), 0);
}

// 2 parameters: ULEB128 register number + SLEB128 offset.
DwarfExprEval::Completion DwarfExprEval::OpBregx() {
  UnsignedType reg_val = 0;
  if (!ReadLEBUnsigned(&reg_val))
    return Completion::kSync;
  int reg = static_cast<int>(reg_val);

  SignedType offset = 0;
  if (!ReadLEBSigned(&offset))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString("DW_OP_bregx(" + std::to_string(reg) + ", " + ExprIntToString(offset) + ")",
                        GetRegisterName(reg) + MakeAddString(offset));
  }

  result_type_ = ResultType::kPointer;
  return PushRegisterWithOffset(reg, offset);
}

// Three operands:
//  1. An unsigned LEB128 integer that represents the offset of a debugging information entry in the
//     current compilation unit.
//  2. The second operand is 1-byte unsigned integer that specifies the size of the
//     constant value.
//  3. The sequence of bytes of the given size.
DwarfExprEval::Completion DwarfExprEval::OpConstType(const char* op_name) {
  UnsignedType die_offset = 0;
  if (!ReadLEBUnsigned(&die_offset))
    return Completion::kSync;

  auto size_or = data_extractor_.Read<uint8_t>();
  if (!size_or)
    return ReportError("Unexpected end of DWARF expression.");

  std::vector<uint8_t> data;
  data.resize(*size_or);
  data_extractor_.ReadBytes(*size_or, data.data());

  if (is_string_output()) {
    std::string str = op_name;
    str += "(die_offset=";
    str += to_hex_string(die_offset);
    str += ", data_size=";
    str += std::to_string(*size_or);
    str += ", data_bytes=";
    for (size_t i = 0; i < data.size(); i++) {
      if (i > 0)
        str += " ";
      str += to_hex_string(data[i], 2);
    }
    str += ")";

    return AppendString(str);
  }

  if (data.size() == 0 || data.size() > sizeof(DwarfStackEntry::UnsignedType))
    return ReportError("Data size for DW_OP_const_type is not supported.");

  auto base_type = unit_symbol_factory_.MakeLazyUnitRelative(static_cast<uint64_t>(die_offset))
                       .Get()
                       ->As<BaseType>();
  if (!base_type)
    return ReportError("Invalid type for DW_OP_const_type in expression.");

  Push(DwarfStackEntry(RefPtrTo(base_type), data.data(), data.size()));
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpConvert(const char* op_name) {
  UnsignedType die_offset = 0;
  if (!ReadLEBUnsigned(&die_offset))
    return Completion::kSync;

  if (is_string_output())
    return AppendString(std::string(op_name) + "(die_offset=" + to_hex_string(die_offset) + ")");

  // Stack value being converted.
  if (stack_.empty())
    return ReportError("Stack underflow in DW_OP_convert.");
  DwarfStackEntry old_value = stack_.back();
  stack_.pop_back();

  // Get the type to convert to.
  auto base_type =
      RefPtrTo(unit_symbol_factory_.MakeLazyUnitRelative(static_cast<uint64_t>(die_offset))
                   .Get()
                   ->As<BaseType>());
  if (!base_type)
    return ReportError("Invalid type for DW_OP_convert in expression.");

// Implements a static cast from the old_value.<from_getter>() to the requested base_type.
#define IMPLEMENT_CAST(from_getter)                                                       \
  if (DwarfStackEntry::TreatAsSigned(base_type.get())) {                                  \
    Push(DwarfStackEntry(base_type, static_cast<SignedType>(old_value.from_getter())));   \
  } else if (DwarfStackEntry::TreatAsUnsigned(base_type.get())) {                         \
    Push(DwarfStackEntry(base_type, static_cast<UnsignedType>(old_value.from_getter()))); \
  } else if (DwarfStackEntry::TreatAsFloat(base_type.get())) {                            \
    Push(DwarfStackEntry(base_type, static_cast<float>(old_value.from_getter())));        \
  } else if (DwarfStackEntry::TreatAsDouble(base_type.get())) {                           \
    Push(DwarfStackEntry(base_type, static_cast<double>(old_value.from_getter())));       \
  }

  if (old_value.TreatAsSigned()) {
    IMPLEMENT_CAST(signed_value);
  } else if (old_value.TreatAsUnsigned()) {
    IMPLEMENT_CAST(unsigned_value);
  } else if (old_value.TreatAsFloat()) {
    IMPLEMENT_CAST(float_value);
  } else if (old_value.TreatAsDouble()) {
    IMPLEMENT_CAST(double_value);
  }

#undef IMPLEMENT_CAST

  return Completion::kSync;
}

// Pops the stack and pushes an given-sized value from memory at that location.
DwarfExprEval::Completion DwarfExprEval::OpDeref(uint32_t byte_size, fxl::RefPtr<BaseType> type,
                                                 const char* op_name, bool string_include_size) {
  if (is_string_output()) {
    if (string_include_size)
      return AppendString(std::string(op_name) + "(" + std::to_string(byte_size) + ")");
    return AppendString(op_name);
  }

  if (stack_.empty()) {
    ReportStackUnderflow();
    return Completion::kSync;
  }

  if (byte_size == 0 || byte_size > sizeof(UnsignedType))
    return ReportError(fxl::StringPrintf("Invalid DWARF expression read size: %u", byte_size));

  DwarfStackEntry addr = stack_.back();
  stack_.pop_back();
  if (!addr.TreatAsUnsigned())
    return ReportError("DW_OP_deref trying to dereference a non-unsigned value.");

  ReadMemory(addr.unsigned_value(), byte_size,
             [type](DwarfExprEval* eval, std::vector<uint8_t> data) {
               FX_DCHECK(data.size() <= sizeof(UnsignedType));
               eval->Push(DwarfStackEntry(type, data.data(), data.size()));
             });
  return Completion::kAsync;
}

DwarfExprEval::Completion DwarfExprEval::OpDerefSize() {
  // The operand is a 1-byte unsigned constant following the opcode.
  UnsignedType byte_size = 0;
  if (!ReadUnsigned(1, &byte_size))
    return Completion::kSync;

  // The generic deref path can handle the rest.
  return OpDeref(static_cast<uint32_t>(byte_size), nullptr, "DW_OP_deref_size", true);
}

DwarfExprEval::Completion DwarfExprEval::OpDerefType(const char* op_name) {
  // Like OP_deref_size except a ULEB DIE offset of the type is the 2nd parameter.
  UnsignedType byte_size = 0;
  if (!ReadUnsigned(1, &byte_size))
    return Completion::kSync;

  UnsignedType die_offset;
  if (!ReadLEBUnsigned(&die_offset))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString(std::string(op_name) +
                        "(size=" + std::to_string(static_cast<int>(byte_size)) +
                        ", die_offset=" + to_hex_string(die_offset) + ")");
  }

  // Extract the symbol for the type.
  auto base_type = unit_symbol_factory_.MakeLazyUnitRelative(static_cast<uint64_t>(die_offset))
                       .Get()
                       ->As<BaseType>();
  if (!base_type)
    return ReportError("Invalid type for DW_OP_deref_type in expression.");

  // The generic deref path can handle the rest.
  return OpDeref(static_cast<uint32_t>(byte_size), RefPtrTo(base_type), "DW_OP_deref_type", true);
}

DwarfExprEval::Completion DwarfExprEval::OpOver() {
  if (is_string_output())
    return AppendString("DW_OP_over");

  // Duplicates the next-to-top over the top item.
  if (stack_.size() < 2)
    ReportStackUnderflow();
  else
    Push(stack_[stack_.size() - 2]);
  return Completion::kSync;
}

// 1 parameter: 1-byte stack index from the top to push.
DwarfExprEval::Completion DwarfExprEval::OpPick() {
  UnsignedType index = 0;
  if (!ReadUnsigned(1, &index))
    return Completion::kSync;

  if (is_string_output())
    return AppendString("DW_OP_pick(" + ExprIntToString(index) + ")");

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
  UnsignedType byte_size = 0;
  if (!ReadLEBUnsigned(&byte_size))
    return Completion::kSync;

  // Upper-bound sanity check on the piece size to guard against corrupted or malicious data.
  constexpr UnsignedType kMaxPieceSize = 16384;
  if (byte_size > kMaxPieceSize) {
    return ReportError(
        fxl::StringPrintf("DWARF expression listed a data size of %d which is too large.",
                          static_cast<int>(byte_size)));
  }

  if (is_string_output())
    return AppendString("DW_OP_piece(" + ExprIntToString(byte_size) + ")");

  if (stack_.empty()) {
    // Defining a piece with no previous data on the stack means to write that many bytes that
    // have no known value.
    result_data_.AppendUnknown(byte_size);
    return Completion::kSync;
  }

  DwarfStackEntry source = stack_.back();
  stack_.pop_back();

  if (result_type_ == ResultType::kValue) {
    // Simple case where the source of the "piece" is the value at the top of the stack.
    if (byte_size > sizeof(UnsignedType)) {
      return ReportError(
          fxl::StringPrintf("DWARF expression listed a data size of %d which is too large.",
                            static_cast<int>(byte_size)));
    }

    size_t source_valid_bytes = source.GetByteSize();
    if (byte_size > source_valid_bytes)
      return ReportError("DW_OP_piece attempting to read more bytes than are valid.");

    // We want the low bytes, this assumes little-endian.
    UnsignedType source_value = source.unsigned_value();
    uint8_t source_as_bytes[sizeof(UnsignedType)];
    memcpy(&source_as_bytes, &source_value, sizeof(UnsignedType));
    result_data_.Append(std::begin(source_as_bytes), &source_as_bytes[byte_size]);

    // Reset the expression state to start a new one.
    result_type_ = ResultType::kPointer;
    return Completion::kSync;
  }

  // This is the more complex case where the top of the stack is a pointer to the value in memory.
  // We read that many bytes from memory and add it to the result data.
  if (!source.TreatAsUnsigned())
    return ReportError("DW_OP_piece attempting to dereference invalid type.");
  ReadMemory(source.unsigned_value(), byte_size,
             [](DwarfExprEval* eval, std::vector<uint8_t> data) {
               // Success. Copy to the result.
               eval->result_data_.Append(data);

               // Reset the expression state to start a new one.
               eval->result_type_ = ResultType::kPointer;
             });

  // The ReadMemory call will complete asynchronously.
  return Completion::kAsync;
}

DwarfExprEval::Completion DwarfExprEval::OpPlusUconst() {
  // "Pops the top stack entry, adds it to the unsigned LEB128 constant operand [...] and pushes the
  // result."
  UnsignedType param = 0;
  if (!ReadLEBUnsigned(&param))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString("DW_OP_plus_uconst(" + ExprIntToString(param) + ")",
                        "+ " + ExprIntToString(param));
  }

  if (stack_.empty()) {
    ReportStackUnderflow();
  } else {
    DwarfStackEntry top = stack_.back();
    stack_.pop_back();

    // "interpreted as the same type as the operand popped from the top of the stack."
    if (top.TreatAsUnsigned()) {
      Push(DwarfStackEntry(top.type_ref(), top.unsigned_value() + param));
    } else if (top.TreatAsSigned()) {
      Push(DwarfStackEntry(top.type_ref(), top.signed_value() + static_cast<SignedType>(param)));
    } else if (top.TreatAsFloat()) {
      Push(DwarfStackEntry(top.type_ref(), top.float_value() + static_cast<float>(param)));
    } else if (top.TreatAsDouble()) {
      Push(DwarfStackEntry(top.type_ref(), top.double_value() + static_cast<double>(param)));
    }
  }
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushSigned(int byte_count, const char* op_name) {
  SignedType value = 0;
  if (!ReadSigned(byte_count, &value))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString(std::string(op_name) + "(" + ExprIntToString(value) + ")",
                        "push(" + ExprIntToString(value) + ")");
  }

  Push(DwarfStackEntry(static_cast<UnsignedType>(value)));
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushUnsigned(int byte_count, const char* op_name) {
  UnsignedType value = 0;
  if (!ReadUnsigned(byte_count, &value))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString(std::string(op_name) + "(" + ExprIntToString(value) + ")",
                        "push(" + ExprIntToString(value) + ")");
  }

  Push(DwarfStackEntry(value));
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushLEBSigned() {
  SignedType value = 0;
  if (!ReadLEBSigned(&value))
    return Completion::kSync;

  if (is_string_output())
    return AppendString("DW_OP_consts(" + ExprIntToString(value) + ")",
                        "push(" + ExprIntToString(value) + ")");

  Push(DwarfStackEntry(static_cast<UnsignedType>(value)));
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpPushLEBUnsigned() {
  UnsignedType value = 0;
  if (!ReadLEBUnsigned(&value))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString("DW_OP_constu(" + ExprIntToString(value) + ")",
                        "push(" + ExprIntToString(value) + ")");
  }

  Push(DwarfStackEntry(value));
  return Completion::kSync;
}

// ULEB register #, ULEB DIE offset of its type.
DwarfExprEval::Completion DwarfExprEval::OpRegvalType(const char* op_name) {
  UnsignedType reg_num = 0;
  if (!ReadLEBUnsigned(&reg_num))
    return Completion::kSync;
  int dwarf_reg_int = static_cast<int>(reg_num);

  UnsignedType die_offset;
  if (!ReadLEBUnsigned(&die_offset))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString(std::string(op_name) + "(reg=" + std::to_string(dwarf_reg_int) +
                            ", die_offset=" + to_hex_string(die_offset) + ")",
                        std::string(op_name) + "(" + GetRegisterName(dwarf_reg_int) +
                            ", die_offset=" + to_hex_string(die_offset) + ")");
  }

  // Extract the symbol for the type.
  auto base_type = unit_symbol_factory_.MakeLazyUnitRelative(static_cast<uint64_t>(die_offset))
                       .Get()
                       ->As<BaseType>();
  if (!base_type)
    return ReportError("Invalid type for DW_OP_regval_type in expression.");
  fxl::RefPtr<BaseType> base_type_ref = RefPtrTo(base_type);

  return GetRegisterValue(dwarf_reg_int, [base_type_ref](DwarfExprEval* eval, Completion completion,
                                                         std::optional<UnsignedType> value,
                                                         const debug::RegisterInfo* info) {
    if (value) {
      // Success.
      UnsignedType value_data = *value;
      eval->Push(DwarfStackEntry(std::move(base_type_ref), &value_data, sizeof(value_data)));

      // Reading register data means the result is not constant.
      eval->result_is_constant_ = false;
      eval->current_register_id_ = info->id;

      if (completion == Completion::kAsync)
        eval->ContinueEval();  // Pick up processing at the next instruction.
    }
  });
}

DwarfExprEval::Completion DwarfExprEval::OpReinterpret(const char* op_name) {
  UnsignedType die_offset = 0;
  if (!ReadLEBUnsigned(&die_offset))
    return Completion::kSync;

  if (is_string_output())
    return AppendString(std::string(op_name) + "(die_offset=" + to_hex_string(die_offset) + ")");

  // Stack value being converted.
  if (stack_.empty())
    return ReportError("Stack underflow in DW_OP_convert.");
  DwarfStackEntry old_value = stack_.back();
  stack_.pop_back();

  // Get the type to convert to.
  auto base_type =
      RefPtrTo(unit_symbol_factory_.MakeLazyUnitRelative(static_cast<uint64_t>(die_offset))
                   .Get()
                   ->As<BaseType>());
  if (!base_type)
    return ReportError("Invalid type for DW_OP_convert in expression.");

  // Implements a bit cast from the old_value.<from_getter>() to the requested base_type.
  // This isn't the same as a C++ reinterpret cast because it can convert from e.g. int64 to float.
  //
  // This copies all of the data bytes out of the source and just changes the types because our
  // signed values are stored sign-extended regardless of the requested data type and we want all
  // of the bits.
  Push(DwarfStackEntry(base_type, old_value.data(), old_value.MaxByteSize()));

  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpRot() {
  if (is_string_output())
    return AppendString("DW_OP_rot");

  // Rotates the top 3 entries "down" with wraparound. "The entry at the top of the stack becomes
  // the third stack entry, the second entry becomes the top of the stack, and the third entry
  // becomes the second entry."
  if (stack_.size() < 3) {
    ReportStackUnderflow();
  } else {
    DwarfStackEntry top = stack_[stack_.size() - 1];
    DwarfStackEntry one_back = stack_[stack_.size() - 2];
    DwarfStackEntry two_back = stack_[stack_.size() - 3];

    stack_[stack_.size() - 1] = std::move(one_back);
    stack_[stack_.size() - 2] = std::move(two_back);
    stack_[stack_.size() - 3] = std::move(top);
  }
  return Completion::kSync;
}

// 1 parameter: 2-byte signed constant.
DwarfExprEval::Completion DwarfExprEval::OpSkip() {
  SignedType skip_amount = 0;
  if (!ReadSigned(2, &skip_amount))
    return Completion::kSync;

  if (is_string_output()) {
    return AppendString("DW_OP_skip(" + ExprIntToString(skip_amount) + ")");

    // Don't actually execute the skip in printing mode, because it could skip backwards to do a
    // loop and we would keep printing from there.
  }

  Skip(skip_amount);
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpStackValue() {
  if (is_string_output())
    return AppendString("DW_OP_stack_value");

  // "Specifies that the object does not exist in memory but rather is a constant value. The value
  // from the top of the stack is the value to be used. This is the actual object value and not the
  // location."
  result_type_ = ResultType::kValue;
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpSwap() {
  if (is_string_output())
    return AppendString("DW_OP_swap");

  if (stack_.size() < 2)
    ReportStackUnderflow();
  else
    std::swap(stack_[stack_.size() - 1], stack_[stack_.size() - 2]);
  return Completion::kSync;
}

DwarfExprEval::Completion DwarfExprEval::OpTlsAddr(const char* op_name) {
  if (is_string_output())
    return AppendString(op_name);

  if (stack_.size() < 1) {
    ReportStackUnderflow();
    return Completion::kSync;
  }

  DwarfStackEntry entry = stack_.back();
  stack_.pop_back();
  if (!entry.TreatAsUnsigned())
    return ReportError("Non-unsigned type parameter on stack when computing TLS.");
  UnsignedType tls_offset = entry.unsigned_value();

  auto debug_address = data_provider_->GetDebugAddressForContext(symbol_context_);

  if (!debug_address)
    return ReportError("Debug address unavailable.");

  data_provider_->GetTLSSegment(
      symbol_context_, [tls_offset, weak_eval = weak_factory_.GetWeakPtr()](ErrOr<uint64_t> value) {
        if (!weak_eval) {
          return;
        }

        if (value.has_error()) {
          weak_eval->ReportError(value.err());
          return;
        }

        weak_eval->Push(DwarfStackEntry(static_cast<UnsignedType>(value.value()) + tls_offset));
        weak_eval->ContinueEval();
      });

  return Completion::kAsync;
}

void DwarfExprEval::Skip(SignedType amount) {
  SignedType new_index = static_cast<SignedType>(data_extractor_.cur()) + amount;
  if (new_index < 0) {
    // Skip before beginning is an error.
    ReportError("DWARF expression skips out-of-bounds.");
  }

  // The Seek() call will clamp to the end which will just mark the expression terminated.
  data_extractor_.Seek(static_cast<size_t>(new_index));
}

std::string DwarfExprEval::GetRegisterName(int reg_number) const {
  const debug::RegisterInfo* reg_info =
      data_provider_ ? debug::DWARFToRegisterInfo(data_provider_->GetArch(), reg_number) : nullptr;
  if (!reg_info)  // Fall back on reporting the register
    return "dwarf_register(" + std::to_string(reg_number) + ")";

  return "register(" + reg_info->name + ")";
}

DwarfExprEval::Completion DwarfExprEval::AppendString(const std::string& op_output,
                                                      const std::string& nice_output) {
  FX_DCHECK(is_string_output());  // Must be in string output mode.

  if (!string_output_.empty())
    string_output_.append(", ");

  if (string_output_mode_ == StringOutput::kPretty && !nice_output.empty()) {
    string_output_.append(nice_output);
  } else {
    string_output_.append(op_output);
  }

  return Completion::kSync;
}

}  // namespace zxdb
