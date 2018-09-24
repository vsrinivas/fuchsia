// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_node.h"

#include <stdlib.h>

#include <ostream>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/resolve_member.h"
#include "garnet/bin/zxdb/expr/resolve_pointer.h"
#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"
#include "garnet/bin/zxdb/symbols/array_type.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"
#include "lib/fxl/strings/string_printf.h"

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
  // This manually extracts the value rather than calling PromoteToInt64() so
  // that the result type is exactly the same as the input type.
  //
  // TODO(brettw) when we add more mathematical operations we'll want a
  // more flexible system for getting the results out.
  if (op_token.type() == ExprToken::kMinus) {
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

void AddressOfExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                             EvalCallback cb) const {
  expr_->Eval(context, [cb = std::move(cb)](const Err& err, ExprValue value) {
    if (value.source().type() != ExprValueSource::Type::kMemory) {
      cb(Err("Can't take the address of a temporary."), ExprValue());
    } else {
      // Construct a pointer type to the variable.
      auto ptr_type = fxl::MakeRefCounted<ModifiedType>(
          Symbol::kTagPointerType, LazySymbol(value.type_ref()));

      std::vector<uint8_t> contents;
      contents.resize(sizeof(uint64_t));
      uint64_t address = value.source().address();
      memcpy(&contents[0], &address, sizeof(uint64_t));

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
  left_->Eval(context, [ inner = inner_, context, cb = std::move(cb) ](
                           const Err& err, ExprValue left_value) {
    if (err.has_error()) {
      cb(err, ExprValue());
    } else {
      // "left" has been evaluated, now do "inner".
      inner->Eval(context, [
        context, left_value = std::move(left_value), cb = std::move(cb)
      ](const Err& err, ExprValue inner_value) {
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
  Err promote_err = inner.PromoteToInt64(offset);
  if (promote_err.has_error())
    return promote_err;
  return Err();
}

// static
void ArrayAccessExprNode::DoAccess(fxl::RefPtr<ExprEvalContext> context,
                                   ExprValue left, int64_t offset,
                                   EvalCallback cb) {
  const Type* type = left.type();
  if (!type) {
    cb(Err("Missing type information, please file a bug with repro."),
       ExprValue());
    return;
  }
  type = type->GetConcreteType();  // Skip "const", etc.

  // You can use [] for either pointer or array types.
  const Type* inner_type = nullptr;
  if (const ArrayType* array_type = type->AsArrayType()) {
    inner_type = array_type->value_type().Get()->AsType();
  } else if (const ModifiedType* mod_type = type->AsModifiedType()) {
    if (mod_type->tag() == Symbol::kTagPointerType)
      inner_type = mod_type->modified().Get()->AsType();
  }
  if (!inner_type) {
    cb(Err("Attempting to use [] on a non-pointer."), ExprValue());
    return;
  }

  // In both pointer and array cases, the data in the value is a pointer
  // to the beginning of the array.
  if (left.data().size() != sizeof(uint64_t)) {
    cb(Err("Incorrect pointer size, please file a bug with a repto."),
       ExprValue());
    return;
  }
  uint64_t array_base = left.GetAs<uint64_t>();
  uint32_t elt_size = inner_type->GetConcreteType()->byte_size();

  ResolvePointer(context->GetDataProvider(), array_base + elt_size * offset,
                 fxl::RefPtr<Type>(const_cast<Type*>(inner_type)),
                 std::move(cb));
}

void ArrayAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ARRAY_ACCESS\n";
  left_->Print(out, indent + 1);
  inner_->Print(out, indent + 1);
}

void DereferenceExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                               EvalCallback cb) const {
  expr_->Eval(context, [ context, cb = std::move(cb) ](const Err& err,
                                                       ExprValue value) {
    ResolvePointer(context->GetDataProvider(), value, std::move(cb));
  });
}

void DereferenceExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "DEREFERENCE\n";
  expr_->Print(out, indent + 1);
}

void IdentifierExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                              EvalCallback cb) const {
  context->GetVariableValue(name_.value(), std::move(cb));
}

void IdentifierExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "IDENTIFIER(" << name_.value() << ")\n";
}

void IntegerExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                           EvalCallback cb) const {
  // The tokenizer will have already validated the integer format.
  cb(Err(), ExprValue(static_cast<int64_t>(atoll(integer_.value().c_str()))));
}

void IntegerExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "INTEGER(" << integer_.value() << ")\n";
}

void MemberAccessExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                                EvalCallback cb) const {
  bool is_arrow = accessor_.type() == ExprToken::kArrow;
  left_->Eval(context, [
    context, is_arrow, member_value = member_.value(), cb = std::move(cb)
  ](const Err& err, ExprValue base) {
    if (!is_arrow) {
      // "." operator.
      ExprValue result;
      Err err = ResolveMember(base, member_value, &result);
      cb(err, std::move(result));
      return;
    }

    // Everything else should be a -> operator.
    ResolveMemberByPointer(
        context, base, member_value,
        [cb = std::move(cb)](const Err& err, ExprValue result) {
          cb(err, std::move(result));
        });
  });
}

void MemberAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ACCESSOR(" << accessor_.value() << ")\n";
  left_->Print(out, indent + 1);
  out << IndentFor(indent + 1) << member_.value() << "\n";
}

void UnaryOpExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                           EvalCallback cb) const {
  expr_->Eval(context, [ cb = std::move(cb), op = op_ ](const Err& err,
                                                        ExprValue value) {
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
