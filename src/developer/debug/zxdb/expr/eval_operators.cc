// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_operators.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/lib/fxl/logging.h"

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
// prefers "int" for smaller types which 32-bit on the current targeted system.
//
// We still more-or-less follow the signed/unsigned rules since sometimes those behaviors are
// important to the result being computed. Effectively, this means using the larger of the two types
// if the type sizes differ, and converting to unsigned if the sizes but sign-edness of the types
// differ.

namespace zxdb {

namespace {

using EvalCallback = fit::callback<void(const Err& err, ExprValue value)>;

void DoAssignment(fxl::RefPtr<EvalContext> context, const ExprValue& left_value,
                  const ExprValue& right_value, EvalCallback cb) {
  // Note: the calling code will have evaluated the value of the left node. Often this isn't
  // strictly necessary: we only need the "source", but optimizing in that way would complicate
  // things.
  const ExprValueSource& dest = left_value.source();
  if (dest.type() == ExprValueSource::Type::kTemporary) {
    cb(Err("Can't assign to a temporary."), ExprValue());
    return;
  }

  // The coerced value will be the result. It should have the "source" of the left-hand-side since
  // the location being assigned to doesn't change.
  ExprValue coerced;
  Err err = CastExprValue(context.get(), CastType::kImplicit, right_value, left_value.type_ref(),
                          &coerced, dest);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  // Make a copy to avoid ambiguity of copying and moving the value below.
  std::vector<uint8_t> data = coerced.data();

  // Update the memory with the new data. The result of the expression is the coerced value.
  context->GetDataProvider()->WriteMemory(
      dest.address(), std::move(data),
      [coerced = std::move(coerced), cb = std::move(cb)](const Err& err) mutable {
        if (err.has_error())
          cb(err, ExprValue());
        else
          cb(Err(), coerced);
      });
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

  return Err("Invalid type for operator.");
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
Err GetOpRealm(fxl::RefPtr<EvalContext>& context, const OpValue& left, const OpValue& right,
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
template <typename T>
Err DoIntBinaryOp(const OpValue& left, const OpValue& right, bool check_for_zero_right,
                  T (*op)(T, T), fxl::RefPtr<Type> result_type, ExprValue* result) {
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

  T result_val = op(left_val, right_val);

  // Never expect to generate larger output than our internal result.
  FXL_DCHECK(result_type->byte_size() <= sizeof(T));

  // Convert to a base type of the correct size.
  std::vector<uint8_t> result_data;
  result_data.resize(result_type->byte_size());
  memcpy(&result_data[0], &result_val, result_type->byte_size());

  *result = ExprValue(std::move(result_type), std::move(result_data));
  return Err();
}

// Converts the given value to a double if possible when
Err OpValueToDouble(EvalContext* context, const OpValue& in, double* out) {
  if (in.realm == MathRealm::kFloat)
    return in.value->PromoteToDouble(out);  // Already floating-point.

  // Needs casting to a float.
  auto double_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double");
  ExprValue casted;
  if (Err err =
          CastExprValue(context, CastType::kImplicit, *in.value, std::move(double_type), &casted);
      err.has_error())
    return err;

  return casted.PromoteToDouble(out);
}

// Applies the given operator to two values that should be done in floating-point.
Err DoFloatBinaryOp(fxl::RefPtr<EvalContext> context, const OpValue& left, const OpValue& right,
                    double (*op)(double, double), fxl::RefPtr<Type> result_type,
                    ExprValue* result) {
  // The inputs could be various types like signed or unsigned integers or even bools. Use the
  // casting infrastructure to convert these when necessary.
  double left_double = 0.0;
  if (Err err = OpValueToDouble(context.get(), left, &left_double); err.has_error())
    return err;
  double right_double = 0.0;
  if (Err err = OpValueToDouble(context.get(), right, &right_double); err.has_error())
    return err;

  // The actual operation.
  double result_double = op(left_double, right_double);

  // Convert to raw bytes.
  std::vector<uint8_t> result_data;
  if (result_type->byte_size() == sizeof(double)) {
    // Result wants a double.
    result_data.resize(sizeof(double));
    memcpy(&result_data[0], &result_double, sizeof(double));
  } else if (result_type->byte_size() == sizeof(float)) {
    // Convert down to 32-bit.
    float result_float = static_cast<float>(result_double);
    result_data.resize(sizeof(float));
    memcpy(&result_data[0], &result_float, sizeof(float));
  } else {
    // No other floating-point sizes are supported.
    return Err("Invalid floating point operation.");
  }

  *result = ExprValue(std::move(result_type), std::move(result_data));
  return Err();
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
Err GetPointedToByteSize(fxl::RefPtr<EvalContext> context, const Type* type, uint32_t* size) {
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

Err DoPointerOperation(fxl::RefPtr<EvalContext> context, const OpValue& left_value,
                       const ExprToken& op, const OpValue& right_value, ExprValue* result) {
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
    *result = ExprValue(result_number, ptr_value->value->type_ref());
    return Err();
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

  *result = ExprValue(static_cast<uint64_t>((left_number - right_number) / left_pointed_to_size),
                      Make64BitIntegerType(MathRealm::kSigned, left_value.concrete_type));
  return Err();
}

}  // namespace

void EvalBinaryOperator(fxl::RefPtr<EvalContext> context, const ExprValue& left_value,
                        const ExprToken& op, const ExprValue& right_value, EvalCallback cb) {
  // Handle assignement specially.
  if (op.type() == ExprTokenType::kEquals)
    return DoAssignment(std::move(context), left_value, right_value, std::move(cb));

  // Left info.
  OpValue left_op_value;
  if (Err err = FillOpValue(context.get(), left_value, &left_op_value); err.has_error())
    return cb(err, ExprValue());

  // Right info.
  OpValue right_op_value;
  if (Err err = FillOpValue(context.get(), right_value, &right_op_value); err.has_error())
    return cb(err, ExprValue());

  // Operation info.
  MathRealm realm;
  fxl::RefPtr<Type> larger_type;
  if (Err err = GetOpRealm(context, left_op_value, right_op_value, &realm, &larger_type);
      err.has_error())
    return cb(err, ExprValue());

  // Special-case pointer operations since they work differently.
  if (realm == MathRealm::kPointer) {
    ExprValue result;
    Err err = DoPointerOperation(context, left_op_value, op, right_op_value, &result);
    return cb(err, std::move(result));
  }

  // Implements the type expansion described at the top of this file.
  larger_type = ExpandTypeTo64(realm, larger_type);

// Implements support for a given operator that only works for integer types.
#define IMPLEMENT_INTEGER_BINARY_OP(c_op, is_divide)                                            \
  switch (realm) {                                                                              \
    case MathRealm::kSigned:                                                                    \
      result_err = DoIntBinaryOp<int64_t>(                                                      \
          left_op_value, right_op_value, is_divide,                                             \
          [](int64_t left, int64_t right) { return left c_op right; }, larger_type, &result);   \
      break;                                                                                    \
    case MathRealm::kUnsigned:                                                                  \
      result_err = DoIntBinaryOp<uint64_t>(                                                     \
          left_op_value, right_op_value, is_divide,                                             \
          [](uint64_t left, uint64_t right) { return left c_op right; }, larger_type, &result); \
      break;                                                                                    \
    case MathRealm::kFloat:                                                                     \
      result_err = Err("Operator '%s' not defined for floating point.", op.value().c_str());    \
      break;                                                                                    \
    case MathRealm::kPointer:                                                                   \
      FXL_NOTREACHED();                                                                         \
      break;                                                                                    \
  }

// Implements support for a given operator that only works for integer or floating point types.
// Pointers should have been handled specially above.
#define IMPLEMENT_BINARY_OP(c_op, is_divide)                                                    \
  switch (realm) {                                                                              \
    case MathRealm::kSigned:                                                                    \
      result_err = DoIntBinaryOp<int64_t>(                                                      \
          left_op_value, right_op_value, is_divide,                                             \
          [](int64_t left, int64_t right) { return left c_op right; }, larger_type, &result);   \
      break;                                                                                    \
    case MathRealm::kUnsigned:                                                                  \
      result_err = DoIntBinaryOp<uint64_t>(                                                     \
          left_op_value, right_op_value, is_divide,                                             \
          [](uint64_t left, uint64_t right) { return left c_op right; }, larger_type, &result); \
      break;                                                                                    \
    case MathRealm::kFloat:                                                                     \
      result_err = DoFloatBinaryOp(                                                             \
          context, left_op_value, right_op_value,                                               \
          [](double left, double right) { return left c_op right; }, larger_type, &result);     \
      break;                                                                                    \
    case MathRealm::kPointer:                                                                   \
      FXL_NOTREACHED();                                                                         \
      break;                                                                                    \
  }

  Err result_err;
  ExprValue result;
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

    case ExprTokenType::kDoubleAnd:
    case ExprTokenType::kLogicalOr:  // Note: implement short-circuiting in the ExprNode!
    case ExprTokenType::kEquality:
      // These all return a bool, need some infrastructure to support that. Fall through to
      // unsupported.
    default:
      cb(Err("Unsupported binary operator '%s', sorry!", op.value().c_str()), ExprValue());
      break;
  }

  cb(result_err, std::move(result));
}

void EvalBinaryOperator(fxl::RefPtr<EvalContext> context, const fxl::RefPtr<ExprNode>& left,
                        const ExprToken& op, const fxl::RefPtr<ExprNode>& right, EvalCallback cb) {
  left->Eval(context, [context, op, right, cb = std::move(cb)](const Err& err,
                                                               ExprValue left_value) mutable {
    if (err.has_error()) {
      cb(err, ExprValue());
      return;
    }

    // Note: if we implement ||, need to special-case here so evaluation short-circuits
    // if the "left" is true.
    right->Eval(context, [context, left_value = std::move(left_value), op, cb = std::move(cb)](
                             const Err& err, ExprValue right_value) mutable {
      EvalBinaryOperator(std::move(context), left_value, op, right_value, std::move(cb));
    });
  });
}

}  // namespace zxdb
