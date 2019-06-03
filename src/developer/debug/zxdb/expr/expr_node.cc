// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_node.h"

#include <stdlib.h>

#include <ostream>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/eval_operators.h"
#include "src/developer/debug/zxdb/expr/expr_eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/number_parser.h"
#include "src/developer/debug/zxdb/expr/resolve_array.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/expr/symbol_variable_resolver.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

std::string IndentFor(int value) { return std::string(value, ' '); }

bool BaseTypeCanBeArrayIndex(const BaseType* type) {
  int bt = type->base_type();
  return bt == BaseType::kBaseTypeBoolean || bt == BaseType::kBaseTypeSigned ||
         bt == BaseType::kBaseTypeSignedChar ||
         bt == BaseType::kBaseTypeUnsigned ||
         bt == BaseType::kBaseTypeUnsignedChar;
}

void EvalUnaryOperator(const ExprToken& op_token, const ExprValue& value,
                       ExprNode::EvalCallback cb) {
  // This manually extracts the value rather than calling PromoteTo64() so
  // that the result type is exactly the same as the input type.
  //
  // TODO(brettw) when we add more mathematical operations we'll want a
  // more flexible system for getting the results out.
  if (op_token.type() == ExprTokenType::kMinus) {
    // Currently "-" is the only unary operator.  Since this is a debugger
    // primarily for C-like languages, use the C rules for negating values: the
    // result type is the same as the input, and negating an unsigned value
    // gives the two's compliment (C++11 standard section 5.3.1).
    switch (value.GetBaseType()) {
      case BaseType::kBaseTypeSigned:
        switch (value.data().size()) {
          case sizeof(int8_t):
            cb(Err(), ExprValue(-value.GetAs<int8_t>()));
            return;
          case sizeof(int16_t):
            cb(Err(), ExprValue(-value.GetAs<int16_t>()));
            return;
          case sizeof(int32_t):
            cb(Err(), ExprValue(-value.GetAs<int32_t>()));
            return;
          case sizeof(int64_t):
            cb(Err(), ExprValue(-value.GetAs<int64_t>()));
            return;
        }
        break;

      case BaseType::kBaseTypeUnsigned:
        switch (value.data().size()) {
          case sizeof(uint8_t):
            cb(Err(), ExprValue(-value.GetAs<uint8_t>()));
            return;
          case sizeof(uint16_t):
            cb(Err(), ExprValue(-value.GetAs<uint16_t>()));
            return;
          case sizeof(uint32_t):
            cb(Err(), ExprValue(-value.GetAs<uint32_t>()));
            return;
          case sizeof(uint64_t):
            cb(Err(), ExprValue(-value.GetAs<uint64_t>()));
            return;
        }
        break;

      default:
        FXL_NOTREACHED();
    }
    cb(Err("Negation for this value is not supported."), ExprValue());
    return;
  }
  FXL_NOTREACHED();
  cb(Err("Internal error evaluating unary operator."), ExprValue());
}

}  // namespace

void ExprNode::EvalFollowReferences(fxl::RefPtr<ExprEvalContext> context,
                                    EvalCallback cb) const {
  Eval(context, [context, cb = std::move(cb)](const Err& err, ExprValue value) {
    if (err.has_error()) {
      cb(err, ExprValue());
    } else {
      EnsureResolveReference(context->GetDataProvider(), std::move(value),
                             std::move(cb));
    }
  });
}

void AddressOfExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                             EvalCallback cb) const {
  expr_->EvalFollowReferences(
      context, [cb = std::move(cb)](const Err& err, ExprValue value) {
        if (value.source().type() != ExprValueSource::Type::kMemory) {
          cb(Err("Can't take the address of a temporary."), ExprValue());
        } else {
          // Construct a pointer type to the variable.
          auto ptr_type = fxl::MakeRefCounted<ModifiedType>(
              DwarfTag::kPointerType, LazySymbol(value.type_ref()));

          std::vector<uint8_t> contents;
          contents.resize(kTargetPointerSize);
          TargetPointer address = value.source().address();
          memcpy(&contents[0], &address, sizeof(kTargetPointerSize));

          cb(Err(), ExprValue(std::move(ptr_type), std::move(contents)));
        }
      });
}

void AddressOfExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ADDRESS_OF\n";
  expr_->Print(out, indent + 1);
}

void ArrayAccessExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                               EvalCallback cb) const {
  left_->EvalFollowReferences(
      context, [inner = inner_, context, cb = std::move(cb)](
                   const Err& err, ExprValue left_value) {
        if (err.has_error()) {
          cb(err, ExprValue());
        } else {
          // "left" has been evaluated, now do "inner".
          inner->EvalFollowReferences(
              context,
              [context, left_value = std::move(left_value), cb = std::move(cb)](
                  const Err& err, ExprValue inner_value) {
                if (err.has_error()) {
                  cb(err, ExprValue());
                } else {
                  // Both "left" and "inner" has been evaluated.
                  int64_t offset = 0;
                  Err offset_err = InnerValueToOffset(inner_value, &offset);
                  if (offset_err.has_error()) {
                    cb(offset_err, ExprValue());
                  } else {
                    DoAccess(std::move(context), std::move(left_value), offset,
                             std::move(cb));
                  }
                }
              });
        }
      });
}

// static
Err ArrayAccessExprNode::InnerValueToOffset(const ExprValue& inner,
                                            int64_t* offset) {
  // Type should be some kind of number.
  const Type* type = inner.type();
  if (!type)
    return Err("Bad type, please file a bug with a repro.");
  type = type->GetConcreteType();  // Skip "const", etc.

  const BaseType* base_type = type->AsBaseType();
  if (!base_type || !BaseTypeCanBeArrayIndex(base_type))
    return Err("Bad type for array index.");

  // This uses signed integers to explicitly allow negative indexing which the
  // user may want to do for some reason.
  Err promote_err = inner.PromoteTo64(offset);
  if (promote_err.has_error())
    return promote_err;
  return Err();
}

// static
void ArrayAccessExprNode::DoAccess(fxl::RefPtr<ExprEvalContext> context,
                                   ExprValue left, int64_t offset,
                                   EvalCallback cb) {
  ResolveArray(
      context->GetDataProvider(), left, static_cast<size_t>(offset),
      static_cast<size_t>(offset) + 1,
      [cb = std::move(cb)](const Err& err, std::vector<ExprValue> result) {
        if (err.has_error()) {
          cb(err, ExprValue());
          return;
        }
        if (result.size() == 0) {
          // Short read, array not big enough.
          cb(Err("Array index out of range."), ExprValue());
          return;
        }
        FXL_DCHECK(result.size() == 1);
        cb(Err(), result[0]);
      });
}

void ArrayAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ARRAY_ACCESS\n";
  left_->Print(out, indent + 1);
  inner_->Print(out, indent + 1);
}

void BinaryOpExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                            EvalCallback cb) const {
  EvalBinaryOperator(std::move(context), left_, op_, right_, std::move(cb));
}

void BinaryOpExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "BINARY_OP(" + op_.value() + ")\n";
  left_->Print(out, indent + 1);
  right_->Print(out, indent + 1);
}

void CastExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                        EvalCallback cb) const {
  // Callback that does the cast given the right type of value.
  auto exec_cast = [context, cast_type = cast_type_, to_type = to_type_->type(),
                    cb = std::move(cb)](const Err& err, ExprValue value) {
    if (err.has_error()) {
      cb(err, value);
    } else {
      ExprValue cast_result;
      Err cast_err =
          CastExprValue(context.get(), cast_type, value, to_type, &cast_result);
      if (cast_err.has_error())
        cb(cast_err, ExprValue());
      else
        cb(Err(), std::move(cast_result));
    }
  };

  from_->Eval(
      context,
      [context, cast_type = cast_type_, to_type = to_type_->type(),
       exec_cast = std::move(exec_cast)](const Err& err, ExprValue value) {
        // This lambda optionally follows the reference on the value according
        // to the requirements of the cast.
        if (err.has_error() || !CastShouldFollowReferences(
                                   context.get(), cast_type, value, to_type)) {
          exec_cast(err, value);  // Also handles the error cases.
        } else {
          EnsureResolveReference(context->GetDataProvider(), std::move(value),
                                 std::move(exec_cast));
        }
      });
}

void CastExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "CAST(" << CastTypeToString(cast_type_) << ")\n";
  to_type_->Print(out, indent + 1);
  from_->Print(out, indent + 1);
}

void DereferenceExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                               EvalCallback cb) const {
  expr_->EvalFollowReferences(
      context, [context, cb = std::move(cb)](const Err& err, ExprValue value) {
        ResolvePointer(context->GetDataProvider(), value, std::move(cb));
      });
}

void DereferenceExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "DEREFERENCE\n";
  expr_->Print(out, indent + 1);
}

void FunctionCallExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                                EvalCallback cb) const {
  cb(Err("Sorry, function calls are not supported."), ExprValue());
}

void FunctionCallExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "FUNCTIONCALL(" << name_.GetDebugName() << ")\n";
  for (const auto& arg : args_)
    arg->Print(out, indent + 1);
}

void IdentifierExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                              EvalCallback cb) const {
  context->GetNamedValue(
      ident_, [cb = std::move(cb)](const Err& err, fxl::RefPtr<Symbol>,
                                   ExprValue value) {
        // Discard resolved symbol, we only need the value.
        cb(err, std::move(value));
      });
}

void IdentifierExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "IDENTIFIER(" << ident_.GetDebugName() << ")\n";
}

void LiteralExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                           EvalCallback cb) const {
  switch (token_.type()) {
    case ExprTokenType::kInteger: {
      ExprValue value;
      Err err = StringToNumber(token_.value(), &value);
      cb(err, std::move(value));
      break;
    }
    case ExprTokenType::kTrue: {
      cb(Err(), ExprValue(true));
      break;
    }
    case ExprTokenType::kFalse: {
      cb(Err(), ExprValue(false));
      break;
    }
    default:
      FXL_NOTREACHED();
  }
}

void LiteralExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "LITERAL(" << token_.value() << ")\n";
}

void MemberAccessExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                                EvalCallback cb) const {
  bool is_arrow = accessor_.type() == ExprTokenType::kArrow;
  left_->EvalFollowReferences(
      context, [context, is_arrow, member = member_, cb = std::move(cb)](
                   const Err& err, ExprValue base) {
        if (!is_arrow) {
          // "." operator.
          ExprValue result;
          Err err = ResolveMember(base, member, &result);
          cb(err, std::move(result));
          return;
        }

        // Everything else should be a -> operator.
        ResolveMemberByPointer(
            context, base, member,
            [cb = std::move(cb)](const Err& err, fxl::RefPtr<Symbol>,
                                 ExprValue result) {
              // Discard resolved symbol, we only need the value.
              cb(err, std::move(result));
            });
      });
}

void MemberAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ACCESSOR(" << accessor_.value() << ")\n";
  left_->Print(out, indent + 1);
  out << IndentFor(indent + 1) << member_.GetFullName() << "\n";
}

void SizeofExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                          EvalCallback cb) const {
  if (const TypeExprNode* type_node =
          const_cast<ExprNode*>(expr_.get())->AsType()) {
    // Types just get used directly.
    SizeofType(type_node->type().get(), std::move(cb));
  } else {
    // Everything else gets evaluated. Strictly C++ won't do this because it's
    // statically typed, but our expression system is not. This doesn't need to
    // follow references because we only need the type and the
    expr_->Eval(std::move(context),
                [cb = std::move(cb)](const Err& err, ExprValue value) {
                  if (err.has_error())
                    cb(err, ExprValue());
                  else
                    SizeofType(value.type(), std::move(cb));
                });
  }
}

void SizeofExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "SIZEOF\n";
  expr_->Print(out, indent + 1);
}

// static
void SizeofExprNode::SizeofType(const Type* in_type, EvalCallback cb) {
  // References should get stripped (sizeof(char&) = 1).
  if (!in_type) {
    cb(Err("Can't do sizeof on a null type."), ExprValue());
    return;
  }

  const Type* type = in_type->GetConcreteType();
  if (DwarfTagIsEitherReference(type->tag()))
    type = type->AsModifiedType()->modified().Get()->AsType();
  if (!type) {
    cb(Err("Symbol error for '%s'.", in_type->GetFullName().c_str()),
       ExprValue());
    return;
  }

  cb(Err(), ExprValue(type->byte_size()));
}

void TypeExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                        EvalCallback cb) const {
  // Doesn't make sense to evaluate a type, callers like casts that expect a
  // type name will look into the node themselves.
  cb(Err("Can not evaluate a type name."), ExprValue());
}

void TypeExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "TYPE(";
  if (type_)
    out << type_->GetFullName();
  out << ")\n";
}

void UnaryOpExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                           EvalCallback cb) const {
  expr_->EvalFollowReferences(
      context, [cb = std::move(cb), op = op_](const Err& err, ExprValue value) {
        if (err.has_error())
          cb(err, std::move(value));
        else
          EvalUnaryOperator(op, value, std::move(cb));
      });
}

void UnaryOpExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "UNARY(" << op_.value() << ")\n";
  expr_->Print(out, indent + 1);
}

}  // namespace zxdb
