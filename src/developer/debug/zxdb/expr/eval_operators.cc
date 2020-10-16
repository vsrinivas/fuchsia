// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_operators.h"

#include <lib/syslog/cpp/macros.h>

#include <type_traits>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/bitfield.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

// About math handling
// -------------------
//
// C++ applies "integer promotion" to doing arithmetic operations. This is a set of rules for
// promoting the parameters to larger types. See:
//   https://en.cppreference.com/w/cpp/language/operator_arithmetic#Conversions
//
// When evaluating expressions in a debugger, the user expects more calculator-like behavior and
// cares less about specific types and truncation rules. As an example, in C++ multiplying two
// integers will yield an integer type that may overflow. But in a debugger expression truncating
// an overflowing value is extremely undesirable.
//
// As a result we upcast all integer operations to 64-bit. This is in contrast to C++ which often
// prefers "int" which are often 32 bits.
//
// We still more-or-less follow the signed/unsigned rules since sometimes those behaviors are
// important to the result being computed. Effectively, this means using the larger of the two types
// if the type sizes differ, and converting to unsigned if the sizes but sign-edness of the types
// differ.

namespace zxdb {

namespace {

using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;
using debug_ipc::RegisterInfo;

// Backend for register assignment that takes the known current value of the destination register
// as well as the new value (possibly in a subrange) and updates the value. This is updated
// according to the used bits and shift amount.
void AssignRegisterWithExistingValue(const fxl::RefPtr<EvalContext>& context,
                                     const ExprValueSource& dest,
                                     std::vector<uint8_t> existing_data, const RegisterInfo& info,
                                     const ExprValue& source,
                                     SymbolDataProvider::WriteCallback cb) {
  // Here we want to support vector registers so can't always bring the result into a numeric
  // variable. These large values are always multiples of bytes (not random bit ranges within
  // bytes). Sometimes bitfields with arbitrary ranges can be brought into registers, but this will
  // always be normal smaller ones that can be used with numbers.
  //
  // These computations assume little-endian.
  if (dest.bit_shift() % 8 == 0 && dest.bit_size() % 8 == 0) {
    // Easy case of everything being byte-aligned. This can handle all vector registers.

    // We expect all non-canonical registers to be byte-aligned inside their canonical one.
    FX_DCHECK(info.bits % 8 == 0);
    FX_DCHECK(info.shift % 8 == 0);

    // In little-endian, the byte shift (from the low bit) just measures from the [0] byte.
    //
    // Do these computations in signed numbers because weird symbol data could give
    // data.size() - offset => negative number.
    int byte_shift = static_cast<int>((dest.bit_shift() + info.shift) / 8);
    int byte_length = std::min(static_cast<int>(dest.bit_size()), info.bits) / 8;

    // Clamp the range to within the buffer in case anything is corrupted.
    byte_length = std::min(byte_length, std::max(0, static_cast<int>(existing_data.size()) -
                                                        byte_shift - byte_length));

    if (byte_length > 0) {
      memcpy(&existing_data[byte_shift], &source.data()[0], byte_length);
      context->GetDataProvider()->WriteRegister(info.canonical_id, std::move(existing_data),
                                                std::move(cb));
    } else {
      // Nothing to write, the symbol shifts seem messed up.
      cb(Err("Could not write register data of %d bytes at offset %d bytes.", byte_length,
             byte_shift));
    }
  } else if (existing_data.size() < sizeof(uint128_t) && source.data().size() < sizeof(uint128_t)) {
    // Have non-byte-sized shifts, the source is probably a bitfield. This assumes little-endian.
    uint128_t existing_value = 0;
    memcpy(&existing_value, &existing_data[0], existing_data.size());

    uint128_t write_value = 0;
    memcpy(&write_value, &source.data()[0], source.data().size());

    // This ExprValueSource takes into account any non-canonical register shifts on top of what
    // may already be there.
    ExprValueSource new_dest(info.canonical_id,
                             std::max(dest.bit_size(), static_cast<uint32_t>(info.bits)),
                             dest.bit_shift() + info.shift);

    uint128_t new_value = new_dest.SetBits(existing_value, write_value);
    memcpy(&existing_data[0], &new_value, existing_data.size());

    context->GetDataProvider()->WriteRegister(info.canonical_id, std::move(existing_data),
                                              std::move(cb));
  } else {
    cb(Err("Can't write bitfield of size %zu to register of size %zu.", source.data().size(),
           existing_data.size()));
  }
}

void DoRegisterAssignment(const fxl::RefPtr<EvalContext>& context, const ExprValueSource& dest,
                          const ExprValue& source, EvalCallback cb) {
  const RegisterInfo* info = debug_ipc::InfoForRegister(dest.register_id());
  if (!info)
    return cb(Err("Assignment to invalid register %u.", dest.register_id()));

  // Transforms a register write callback (Err only) to a EvalCallback (ErrOr<ExprValue>).
  SymbolDataProvider::WriteCallback write_cb = [source,
                                                cb = std::move(cb)](const Err& err) mutable {
    if (err.has_error())
      cb(err);
    else
      cb(source);
  };

  if (info->canonical_id == dest.register_id() && !dest.is_bitfield()) {
    // Normal register write with no masking or shifting.
    context->GetDataProvider()->WriteRegister(dest.register_id(), source.data(),
                                              std::move(write_cb));
  } else {
    // This write requires some masking and shifting, and therefore needs the current register
    // value.
    context->GetDataProvider()->GetRegisterAsync(
        info->canonical_id, [context, source, dest, info = *info, write_cb = std::move(write_cb)](
                                const Err& err, std::vector<uint8_t> data) mutable {
          if (err.has_error()) {
            write_cb(err);
          } else {
            AssignRegisterWithExistingValue(context, dest, std::move(data), info, source,
                                            std::move(write_cb));
          }
        });
  }
}

void DoMemoryAssignment(const fxl::RefPtr<EvalContext>& context, const ExprValueSource& dest,
                        const ExprValue& source, EvalCallback cb) {
  // Update the memory with the new data. The result of the expression is the coerced value.
  auto write_callback = [source, cb = std::move(cb)](const Err& err) mutable {
    if (err.has_error())
      cb(err);
    else
      cb(source);
  };
  if (dest.is_bitfield()) {
    WriteBitfieldToMemory(context, dest, source.data(), std::move(write_callback));
  } else {
    // Normal case for non-bitfields.
    context->GetDataProvider()->WriteMemory(dest.address(), source.data(),
                                            std::move(write_callback));
  }
}

void DoAssignment(const fxl::RefPtr<EvalContext>& context, const ExprValue& left_value,
                  const ExprValue& right_value, EvalCallback cb) {
  if (left_value.data().size() == 0)
    return cb(Err("Can't assign 0-size value."));

  // Note: the calling code will have evaluated the value of the left node. Often this isn't
  // strictly necessary: we only need the "source", but optimizing in that way would complicate
  // things.
  const ExprValueSource& dest = left_value.source();
  if (dest.type() == ExprValueSource::Type::kTemporary)
    return cb(Err("Can't assign to a temporary."));
  if (dest.type() == ExprValueSource::Type::kConstant)
    return cb(Err("Can't assign to a constant."));
  if (dest.type() == ExprValueSource::Type::kComposite) {
    // TODO(bug 39630) implement composite variable locations.
    return cb(Err("Can't assign to a composite variable location (see bug 39630)."));
  }

  // The coerced value will be the result. It should have the "source" of the left-hand-side since
  // the location being assigned to doesn't change.
  CastExprValue(context, CastType::kImplicit, right_value, left_value.type_ref(), ExprValueSource(),
                [context, dest, cb = std::move(cb)](ErrOrValue coerced) mutable {
                  if (coerced.has_error())
                    return cb(coerced);

                  if (dest.type() == ExprValueSource::Type::kRegister) {
                    DoRegisterAssignment(context, dest, coerced.value(), std::move(cb));
                  } else {
                    DoMemoryAssignment(context, dest, coerced.value(), std::move(cb));
                  }
                });
}

// This is used as the return type for comparison operations.
fxl::RefPtr<BaseType> MakeBoolType() {
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool");
}

// The "math realm" is the type of operation being done, since operators in these different spaces
// have very different behaviors.
enum class MathRealm { kSigned, kUnsigned, kFloat, kPointer };

bool IsIntegerRealm(MathRealm realm) {
  return realm == MathRealm::kSigned || realm == MathRealm::kUnsigned;
}

// Computes how math should be done on the given type. The type should be concrete.
Err GetRealm(const Type* type, MathRealm* realm) {
  // Check for pointers.
  if (const ModifiedType* mod = type->AsModifiedType()) {
    if (mod->tag() == DwarfTag::kPointerType) {
      *realm = MathRealm::kPointer;
      return Err();
    }
  } else if (const BaseType* base = type->AsBaseType()) {
    // Everything else should be a base type.
    switch (base->base_type()) {
      case BaseType::kBaseTypeNone:
        break;  // Error, fall through to bottom of function.

      case BaseType::kBaseTypeAddress:
        *realm = MathRealm::kPointer;
        return Err();

      case BaseType::kBaseTypeFloat:
        *realm = MathRealm::kFloat;
        return Err();
    }

    if (BaseType::IsSigned(base->base_type()))
      *realm = MathRealm::kSigned;
    else
      *realm = MathRealm::kUnsigned;
    return Err();
  }

  return Err("Invalid non-numeric type '%s' for operator.", type->GetFullName().c_str());
}

// Collects the computed information for one parameter for passing around more conveniently.
struct OpValue {
  const ExprValue* value = nullptr;
  fxl::RefPtr<Type> concrete_type;  // Extracted from value.type().
  MathRealm realm = MathRealm::kUnsigned;
};

Err FillOpValue(EvalContext* context, const ExprValue& in, OpValue* out) {
  out->value = &in;

  out->concrete_type = context->GetConcreteType(in.type());
  if (!out->concrete_type)
    return Err("No type information");
  if (out->concrete_type->byte_size() == 0 || in.data().empty())
    return Err("Empty type size for operator.");

  return GetRealm(out->concrete_type.get(), &out->realm);
}

// Given a binary operation of the two parameters, computes the realm that the operation should be
// done in, and computes which of the types is larger. This larger type does not take into account
// integral promotion described at the top of this file, it will always be one of the two inputs.
Err GetOpRealm(const fxl::RefPtr<EvalContext>& context, const OpValue& left, const OpValue& right,
               MathRealm* op_realm, fxl::RefPtr<Type>* larger_type) {
  // Pointer always takes precedence.
  if (left.realm == MathRealm::kPointer) {
    *op_realm = left.realm;
    *larger_type = left.concrete_type;
    return Err();
  }
  if (right.realm == MathRealm::kPointer) {
    *op_realm = right.realm;
    *larger_type = right.concrete_type;
    return Err();
  }

  // Floating-point is next.
  if (left.realm == MathRealm::kFloat && right.realm == MathRealm::kFloat) {
    // Both float: pick the biggest one (defaulting to the left on a tie).
    *op_realm = MathRealm::kFloat;
    if (right.concrete_type->byte_size() > left.concrete_type->byte_size())
      *larger_type = right.concrete_type;
    else
      *larger_type = left.concrete_type;
    return Err();
  }
  if (left.realm == MathRealm::kFloat) {
    *op_realm = MathRealm::kFloat;
    *larger_type = left.concrete_type;
    return Err();
  }
  if (right.realm == MathRealm::kFloat) {
    *op_realm = MathRealm::kFloat;
    *larger_type = right.concrete_type;
    return Err();
  }

  // Integer math. Pick the larger one if the sizes are different.
  if (left.concrete_type->byte_size() > right.concrete_type->byte_size()) {
    *op_realm = left.realm;
    *larger_type = left.concrete_type;
    return Err();
  }
  if (right.concrete_type->byte_size() > left.concrete_type->byte_size()) {
    *op_realm = right.realm;
    *larger_type = right.concrete_type;
    return Err();
  }

  // Same size and both are integers, pick the unsigned one if they disagree.
  if (left.realm != right.realm) {
    if (left.realm == MathRealm::kUnsigned) {
      *op_realm = left.realm;
      *larger_type = left.concrete_type;
    } else {
      *op_realm = right.realm;
      *larger_type = right.concrete_type;
    }
    return Err();
  }

  // Pick the left one if everything else agrees.
  *op_realm = left.realm;
  *larger_type = left.concrete_type;
  return Err();
}

// Applies the given operator to two integers. The type T can be either uint64_t for unsigned, or
// int64_t for signed operation.
//
// The flag "check_for_zero_right" will issue a divide-by-zero error if the right-hand-side is zero.
// Error checking could be generalized more in the "op" callback, but this is currently the only
// error case and it keeps all of the op implementations simpler to do it this way.
template <typename T, typename ResultT>
ErrOrValue DoIntBinaryOp(const OpValue& left, const OpValue& right, bool check_for_zero_right,
                         ResultT (*op)(T, T), fxl::RefPtr<Type> result_type) {
  T left_val;
  if (Err err = left.value->PromoteTo64(&left_val); err.has_error())
    return err;

  T right_val;
  if (Err err = right.value->PromoteTo64(&right_val); err.has_error())
    return err;
  if (check_for_zero_right) {
    if (right_val == 0)
      return Err("Division by 0.");
  }

  ResultT result_val = op(left_val, right_val);

  // Never expect to generate larger output than our internal result.
  FX_DCHECK(result_type->byte_size() <= sizeof(ResultT));

  // Convert to a base type of the correct size.
  std::vector<uint8_t> result_data;
  result_data.resize(result_type->byte_size());
  memcpy(&result_data[0], &result_val, result_type->byte_size());

  return ExprValue(std::move(result_type), std::move(result_data));
}

// Converts the given value to a double if possible when
Err OpValueToDouble(const fxl::RefPtr<EvalContext>& context, const OpValue& in, double* out) {
  if (in.realm == MathRealm::kFloat)
    return in.value->PromoteToDouble(out);  // Already floating-point.

  // Needs casting to a float.
  auto double_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double");
  ErrOrValue casted = CastNumericExprValue(context, *in.value, std::move(double_type));
  if (casted.has_error())
    return casted.err();

  return casted.value().PromoteToDouble(out);
}

// Applies the given operator to two values that should be done in floating-point. The templated
// result type should be either a double (for math) or bool (for comparison). In the boolean case,
// the result_typee may be null since this will be the autmoatically created one.
template <typename ResultT>
ErrOrValue DoFloatBinaryOp(const fxl::RefPtr<EvalContext>& context, const OpValue& left,
                           const OpValue& right, ResultT (*op)(double, double),
                           fxl::RefPtr<Type> result_type) {
  // The inputs could be various types like signed or unsigned integers or even bools. Use the
  // casting infrastructure to convert these when necessary.
  double left_double = 0.0;
  if (Err err = OpValueToDouble(context, left, &left_double); err.has_error())
    return err;
  double right_double = 0.0;
  if (Err err = OpValueToDouble(context, right, &right_double); err.has_error())
    return err;

  // The actual operation.
  ResultT result_val = op(left_double, right_double);

  // Convert to raw bytes.
  std::vector<uint8_t> result_data;
  if (std::is_same<ResultT, bool>::value)  // Result wants a boolean.
    return ExprValue(result_val);
  if (result_type->byte_size() == sizeof(double))  // Result wants a double.
    return ExprValue(result_val, std::move(result_type));
  if (result_type->byte_size() == sizeof(float))  // Convert down to 32-bit float.
    return ExprValue(static_cast<float>(result_val), std::move(result_type));

  // No other floating-point sizes are supported.
  return Err("Invalid floating point operation.");
}

// Returns a language-appropriate 64-bit signed or unsigned (according to the realm) type. The
// language is taken from the given language reference type.
fxl::RefPtr<Type> Make64BitIntegerType(MathRealm realm, fxl::RefPtr<Type> lang_reference) {
  bool is_rust = lang_reference->GetLanguage() == DwarfLang::kRust;

  if (realm == MathRealm::kSigned) {
    if (is_rust)
      return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "i64");
    return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "int64_t");
  } else if (realm == MathRealm::kUnsigned) {
    if (is_rust)
      return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "u64");
    return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "uint64_t");
  }
  return fxl::RefPtr<Type>();
}

// Computes a possibly-new larger type for the given math realm. This is so we can avoid overflow
// when using expressions in "calculator" mode regardless of the input type.
fxl::RefPtr<Type> ExpandTypeTo64(MathRealm realm, fxl::RefPtr<Type> input) {
  if (input->byte_size() >= 8)
    return input;  // 64-bit input is large enough, don't mess with it.

  // Smaller ints get a synthesized type.
  if (realm == MathRealm::kSigned || realm == MathRealm::kUnsigned)
    return Make64BitIntegerType(realm, input);

  // No change necessary. Don't change floats or pointers.
  return input;
}

// Returns the byte size of the type pointed to by the given type. If anything fails or if the size
// is 0, returns an error.
Err GetPointedToByteSize(const fxl::RefPtr<EvalContext>& context, const Type* type,
                         uint32_t* size) {
  *size = 0;

  fxl::RefPtr<Type> pointed_to;
  if (Err err = GetPointedToType(context, type, &pointed_to); err.has_error())
    return err;

  // Need to make concrete to get the size.
  pointed_to = context->GetConcreteType(pointed_to.get());
  *size = pointed_to->byte_size();
  if (*size == 0)
    return Err("Can't do pointer arithmetic on a type of size 0.");
  return Err();
}

ErrOrValue DoPointerOperation(const fxl::RefPtr<EvalContext>& context, const OpValue& left_value,
                              const ExprToken& op, const OpValue& right_value) {
  // Adding or subtracting a pointer and an integer or and integer to a pointer advances the pointer
  // by the size of the pointed-to type.
  const OpValue* int_value = nullptr;
  const OpValue* ptr_value = nullptr;
  if (left_value.realm == MathRealm::kPointer && IsIntegerRealm(right_value.realm)) {
    // pointer <op> int: Addition and subtraction are supported.
    if (op.type() != ExprTokenType::kMinus && op.type() != ExprTokenType::kPlus)
      return Err("Unsupported operator '%s' for pointer.", op.value().c_str());

    ptr_value = &left_value;
    int_value = &right_value;
  } else if (IsIntegerRealm(left_value.realm) && right_value.realm == MathRealm::kPointer) {
    // int <op> pointer: Only addition is supported.
    if (op.type() != ExprTokenType::kPlus)
      return Err("Unsupported operator '%s' for pointer.", op.value().c_str());

    int_value = &left_value;
    ptr_value = &right_value;
  }
  if (int_value && ptr_value) {
    uint32_t pointed_to_size = 0;
    if (Err err = GetPointedToByteSize(context, ptr_value->concrete_type.get(), &pointed_to_size);
        err.has_error())
      return err;

    uint64_t ptr_number = 0;
    if (Err err = ptr_value->value->PromoteTo64(&ptr_number); err.has_error())
      return err;

    int64_t int_number = 0;
    if (Err err = int_value->value->PromoteTo64(&int_number); err.has_error())
      return err;

    uint64_t result_number;
    if (op.type() == ExprTokenType::kMinus) {
      // For minus everything was checked above so we know this is <pointer> - <number>.
      result_number = ptr_number - pointed_to_size * int_number;
    } else {
      // Everything else should be addition.
      result_number = ptr_number + pointed_to_size * int_number;
    }

    // Convert to the result. Use the type from the pointer on the value to keep things like C-V
    // qualifiers from the original.
    return ExprValue(result_number, ptr_value->value->type_ref());
  }

  // The only other pointer operation to support is subtraction.
  if (op.type() != ExprTokenType::kMinus)
    return Err("Unsupported operator '%s' for pointer.", op.value().c_str());

  // For subtraction, both pointers need to be the same type.
  if (left_value.concrete_type->GetFullName() != right_value.concrete_type->GetFullName()) {
    return Err("Can't subtract pointers of different types '%s' and '%s'.",
               left_value.concrete_type->GetFullName().c_str(),
               right_value.concrete_type->GetFullName().c_str());
  }

  // Validate the pointed-to type sizes.
  uint32_t left_pointed_to_size = 0;
  if (Err err =
          GetPointedToByteSize(context, left_value.concrete_type.get(), &left_pointed_to_size);
      err.has_error())
    return err;
  uint32_t right_pointed_to_size = 0;
  if (Err err =
          GetPointedToByteSize(context, right_value.concrete_type.get(), &right_pointed_to_size);
      err.has_error())
    return err;
  if (left_pointed_to_size != right_pointed_to_size) {
    return Err("Can't subtract pointers of different sizes %" PRIu32 " and %" PRIu32 ".",
               left_pointed_to_size, right_pointed_to_size);
  }

  // Do the operation in signed so that subtraction makes sense (ptrdiff_t is signed).
  int64_t left_number = 0;
  if (Err err = left_value.value->PromoteTo64(&left_number); err.has_error())
    return err;
  int64_t right_number = 0;
  if (Err err = right_value.value->PromoteTo64(&right_number); err.has_error())
    return err;

  return ExprValue(static_cast<uint64_t>((left_number - right_number) / left_pointed_to_size),
                   Make64BitIntegerType(MathRealm::kSigned, left_value.concrete_type));
}

ErrOrValue DoLogicalBinaryOp(const fxl::RefPtr<EvalContext>& context, const OpValue& left_value,
                             const ExprToken& op, const OpValue& right_value) {
  // In general the left will have already been converted to a bool and checks to implement
  // short-ciruiting for these operators. But reevaluate anyway which is useful for tests.
  ErrOrValue left_as_bool = CastNumericExprValue(context, *left_value.value, MakeBoolType());
  if (left_as_bool.has_error())
    return left_as_bool;

  ErrOrValue right_as_bool = CastNumericExprValue(context, *right_value.value, MakeBoolType());
  if (right_as_bool.has_error())
    return right_as_bool;

  if (op.type() == ExprTokenType::kDoubleAnd) {
    return ExprValue(left_as_bool.value().GetAs<uint8_t>() &&
                     right_as_bool.value().GetAs<uint8_t>());
  }
  if (op.type() == ExprTokenType::kLogicalOr) {
    return ExprValue(left_as_bool.value().GetAs<uint8_t>() ||
                     right_as_bool.value().GetAs<uint8_t>());
  }
  return Err("Internal error.");
}

}  // namespace

void EvalBinaryOperator(const fxl::RefPtr<EvalContext>& context, const ExprValue& left_value,
                        const ExprToken& op, const ExprValue& right_value, EvalCallback cb) {
  if (!left_value.type() || !right_value.type())
    return cb(Err("No type information."));

  // Handle assignement specially.
  if (op.type() == ExprTokenType::kEquals)
    return DoAssignment(std::move(context), left_value, right_value, std::move(cb));

  // Left info.
  OpValue left_op_value;
  if (Err err = FillOpValue(context.get(), left_value, &left_op_value); err.has_error())
    return cb(err);

  // Right info.
  OpValue right_op_value;
  if (Err err = FillOpValue(context.get(), right_value, &right_op_value); err.has_error())
    return cb(err);

  // Operation info.
  MathRealm realm;
  fxl::RefPtr<Type> larger_type;
  if (Err err = GetOpRealm(context, left_op_value, right_op_value, &realm, &larger_type);
      err.has_error())
    return cb(err);

  // Special-case pointer operations since they work differently.
  if (realm == MathRealm::kPointer)
    return cb(DoPointerOperation(context, left_op_value, op, right_op_value));

  // Implements the type expansion described at the top of this file.
  larger_type = ExpandTypeTo64(realm, larger_type);

// Implements support for a given operator that only works for integer types.
#define IMPLEMENT_INTEGER_BINARY_OP(c_op, is_divide)                                     \
  switch (realm) {                                                                       \
    case MathRealm::kSigned:                                                             \
      result = DoIntBinaryOp<int64_t, int64_t>(                                          \
          left_op_value, right_op_value, is_divide,                                      \
          [](int64_t left, int64_t right) { return left c_op right; }, larger_type);     \
      break;                                                                             \
    case MathRealm::kUnsigned:                                                           \
      result = DoIntBinaryOp<uint64_t, uint64_t>(                                        \
          left_op_value, right_op_value, is_divide,                                      \
          [](uint64_t left, uint64_t right) { return left c_op right; }, larger_type);   \
      break;                                                                             \
    case MathRealm::kFloat:                                                              \
      result = Err("Operator '%s' not defined for floating point.", op.value().c_str()); \
      break;                                                                             \
    case MathRealm::kPointer:                                                            \
      FX_NOTREACHED();                                                                   \
      break;                                                                             \
  }

// Implements support for a given operator that only works for integer or floating point types.
// Pointers should have been handled specially above.
#define IMPLEMENT_BINARY_OP(c_op, is_divide)                                           \
  switch (realm) {                                                                     \
    case MathRealm::kSigned:                                                           \
      result = DoIntBinaryOp<int64_t, int64_t>(                                        \
          left_op_value, right_op_value, is_divide,                                    \
          [](int64_t left, int64_t right) { return left c_op right; }, larger_type);   \
      break;                                                                           \
    case MathRealm::kUnsigned:                                                         \
      result = DoIntBinaryOp<uint64_t, uint64_t>(                                      \
          left_op_value, right_op_value, is_divide,                                    \
          [](uint64_t left, uint64_t right) { return left c_op right; }, larger_type); \
      break;                                                                           \
    case MathRealm::kFloat:                                                            \
      result = DoFloatBinaryOp<double>(                                                \
          context, left_op_value, right_op_value,                                      \
          [](double left, double right) { return left c_op right; }, larger_type);     \
      break;                                                                           \
    case MathRealm::kPointer:                                                          \
      FX_NOTREACHED();                                                                 \
      break;                                                                           \
  }

// Implements support for a given comparison operator.
#define IMPLEMENT_COMPARISON_BINARY_OP(c_op)                                              \
  switch (realm) {                                                                        \
    case MathRealm::kSigned:                                                              \
      result = DoIntBinaryOp<int64_t, bool>(                                              \
          left_op_value, right_op_value, false,                                           \
          [](int64_t left, int64_t right) { return left c_op right; }, MakeBoolType());   \
      break;                                                                              \
    case MathRealm::kUnsigned:                                                            \
      result = DoIntBinaryOp<uint64_t, bool>(                                             \
          left_op_value, right_op_value, false,                                           \
          [](uint64_t left, uint64_t right) { return left c_op right; }, MakeBoolType()); \
      break;                                                                              \
    case MathRealm::kFloat:                                                               \
      result = DoFloatBinaryOp<bool>(                                                     \
          context, left_op_value, right_op_value,                                         \
          [](double left, double right) { return left c_op right; }, nullptr);            \
      break;                                                                              \
    case MathRealm::kPointer:                                                             \
      FX_NOTREACHED();                                                                    \
      break;                                                                              \
  }

  ErrOrValue result((ExprValue()));
  switch (op.type()) {
    case ExprTokenType::kPlus:
      IMPLEMENT_BINARY_OP(+, false);
      break;
    case ExprTokenType::kMinus:
      IMPLEMENT_BINARY_OP(-, false);
      break;
    case ExprTokenType::kSlash:
      IMPLEMENT_BINARY_OP(/, true);
      break;
    case ExprTokenType::kStar:
      IMPLEMENT_BINARY_OP(*, false);
      break;
    case ExprTokenType::kPercent:
      IMPLEMENT_INTEGER_BINARY_OP(%, true);
      break;
    case ExprTokenType::kAmpersand:
      IMPLEMENT_INTEGER_BINARY_OP(&, false);
      break;
    case ExprTokenType::kBitwiseOr:
      IMPLEMENT_INTEGER_BINARY_OP(|, false);
      break;
    case ExprTokenType::kCaret:
      IMPLEMENT_INTEGER_BINARY_OP(^, false);
      break;
    case ExprTokenType::kShiftLeft:
      IMPLEMENT_INTEGER_BINARY_OP(<<, false);
      break;
    case ExprTokenType::kShiftRight:
      IMPLEMENT_INTEGER_BINARY_OP(>>, false);
      break;

    case ExprTokenType::kEquality:
      IMPLEMENT_COMPARISON_BINARY_OP(==);
      break;
    case ExprTokenType::kInequality:
      IMPLEMENT_COMPARISON_BINARY_OP(!=);
      break;
    case ExprTokenType::kLessEqual:
      IMPLEMENT_COMPARISON_BINARY_OP(<=);
      break;
    case ExprTokenType::kGreaterEqual:
      IMPLEMENT_COMPARISON_BINARY_OP(>=);
      break;
    case ExprTokenType::kLess:
      IMPLEMENT_COMPARISON_BINARY_OP(<);
      break;
    case ExprTokenType::kGreater:
      IMPLEMENT_COMPARISON_BINARY_OP(>);
      break;

    case ExprTokenType::kSpaceship:
      // The three-way comparison isn't useful in a debugger, and isn't really implementable anyway
      // because it returns some kind of special std constant that we would rather not count on.
      result = Err("Sorry, no UFOs allowed here.");
      break;

    case ExprTokenType::kDoubleAnd:
    case ExprTokenType::kLogicalOr:
      result = DoLogicalBinaryOp(context, left_op_value, op, right_op_value);
      break;

    default:
      result = Err("Unsupported binary operator '%s', sorry!", op.value().c_str());
      break;
  }

  cb(std::move(result));
}

void EvalBinaryOperator(const fxl::RefPtr<EvalContext>& context, const fxl::RefPtr<ExprNode>& left,
                        const ExprToken& op, const fxl::RefPtr<ExprNode>& right, EvalCallback cb) {
  left->Eval(context, [context, op, right, cb = std::move(cb)](ErrOrValue left_value) mutable {
    if (left_value.has_error())
      return cb(left_value);

    if (op.type() == ExprTokenType::kLogicalOr || op.type() == ExprTokenType::kDoubleAnd) {
      // Short-circuit for || and &&.
      ErrOrValue left_as_bool = CastNumericExprValue(context, left_value.value(), MakeBoolType());
      if (left_as_bool.has_error())
        return cb(left_as_bool.err());

      if (left_as_bool.value().GetAs<uint8_t>()) {
        if (op.type() == ExprTokenType::kLogicalOr)
          return cb(left_as_bool);  // Computation complete, skip evaluating the right side.

        // Fall through to evaluating the right side given the left already casted to a bool.
        left_value = left_as_bool.value();
      } else {
        if (op.type() == ExprTokenType::kDoubleAnd)
          return cb(left_as_bool);  // Computation complete, skip evaluating the right side.

        // Fall through to evaluating the right side given the left already casted to a bool.
        left_value = left_as_bool.value();
      }
    }

    right->Eval(context, [context, left_value = left_value.take_value(), op,
                          cb = std::move(cb)](ErrOrValue right_value) mutable {
      if (right_value.has_error())
        cb(right_value);
      else
        EvalBinaryOperator(std::move(context), left_value, op, right_value.value(), std::move(cb));
    });
  });
}

void EvalUnaryOperator(const fxl::RefPtr<EvalContext>& context, const ExprToken& op_token,
                       const ExprValue& value, EvalCallback cb) {
  if (!value.type())
    return cb(Err("No type information."));

  OpValue op_value;
  if (Err err = FillOpValue(context.get(), value, &op_value); err.has_error())
    return cb(err);

    // Implements a unary operator that applies C rules for the 4 different sized types. The types
    // are passed in so that this can work with both signed and unsigned input.
    //
    // C has a bunch of rules (see "integer promotion" at the top of this file).
    //
    // This logic implicitly takes advantage of the C rules but the type names produced will be the
    // sized C++ stdint.h types rather than what C would use (int/unsigned, etc.) or whatever the
    // current language would produce (e.g. u32 on Rust). Since these are temporaries, the type
    // names usually aren't very important so the simplicity of this approach is preferrable.
#define IMPLEMENT_UNARY_INTEGER_OP(c_op, type8, type16, type32, type64)                    \
  switch (value.data().size()) {                                                           \
    case sizeof(type8):                                                                    \
      result = ExprValue(c_op value.GetAs<type8>());                                       \
      break;                                                                               \
    case sizeof(type16):                                                                   \
      result = ExprValue(c_op value.GetAs<type16>());                                      \
      break;                                                                               \
    case sizeof(type32):                                                                   \
      result = ExprValue(c_op value.GetAs<type32>());                                      \
      break;                                                                               \
    case sizeof(type64):                                                                   \
      result = ExprValue(c_op value.GetAs<type64>());                                      \
      break;                                                                               \
    default:                                                                               \
      result = Err("Unsupported size for unary operator '%s'.", op_token.value().c_str()); \
      break;                                                                               \
  }

#define IMPLEMENT_UNARY_FLOAT_OP(c_op)                                                     \
  switch (value.data().size()) {                                                           \
    case sizeof(float):                                                                    \
      result = ExprValue(c_op value.GetAs<float>());                                       \
      break;                                                                               \
    case sizeof(double):                                                                   \
      result = ExprValue(c_op value.GetAs<double>());                                      \
      break;                                                                               \
    default:                                                                               \
      result = Err("Unsupported size for unary operator '%s'.", op_token.value().c_str()); \
      break;                                                                               \
  }

  ErrOrValue result((ExprValue()));
  switch (op_token.type()) {
    // -
    case ExprTokenType::kMinus:
      switch (op_value.realm) {
        case MathRealm::kSigned:
          IMPLEMENT_UNARY_INTEGER_OP(-, int8_t, int16_t, int32_t, int64_t);
          break;
        case MathRealm::kUnsigned:
          IMPLEMENT_UNARY_INTEGER_OP(-, uint8_t, uint16_t, uint32_t, uint64_t);
          break;
        case MathRealm::kFloat:
          IMPLEMENT_UNARY_FLOAT_OP(-);
          break;
        default:
          result =
              Err("Invalid type '%s' for unary operator '-'.", value.type()->GetFullName().c_str());
          break;
      }
      break;

    // !
    case ExprTokenType::kBang:
      switch (op_value.realm) {
        case MathRealm::kSigned:
          IMPLEMENT_UNARY_INTEGER_OP(!, int8_t, int16_t, int32_t, int64_t);
          break;
        case MathRealm::kPointer:  // ! can treat a pointer like an unsigned int.
        case MathRealm::kUnsigned:
          IMPLEMENT_UNARY_INTEGER_OP(!, uint8_t, uint16_t, uint32_t, uint64_t);
          break;
        case MathRealm::kFloat:
          IMPLEMENT_UNARY_FLOAT_OP(!);
          break;
      }
      break;

    default:
      result = Err("Invalid unary operator '%s'.", op_token.value().c_str());
      break;
  }
  cb(result);
}

}  // namespace zxdb
