// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_node.h"

#include <ostream>

#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

std::string IndentFor(int value) { return std::string(value, ' '); }

void EvalUnaryOperator(const ExprToken& op_token, const ExprValue& value,
                       ExprNode::EvalCallback cb) {
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
          case 1:  // int8_t
            cb(Err(), ExprValue(-value.GetAs<int8_t>()));
            return;
          case 2:  // int16_t
            cb(Err(), ExprValue(-value.GetAs<int16_t>()));
            return;
          case 4:  // int32_t
            cb(Err(), ExprValue(-value.GetAs<int32_t>()));
            return;
          case 8:  // int64_t
            cb(Err(), ExprValue(-value.GetAs<int64_t>()));
            return;
        }
        break;

      case BaseType::kBaseTypeUnsigned:
        switch (value.data().size()) {
          case 1:  // uint8_t
            cb(Err(), ExprValue(-value.GetAs<uint8_t>()));
            return;
          case 2:  // uint16_t
            cb(Err(), ExprValue(-value.GetAs<uint16_t>()));
            return;
          case 4:  // uint32_t
            cb(Err(), ExprValue(-value.GetAs<uint32_t>()));
            return;
          case 8:  // uint64_t
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

void AddressOfExprNode::Eval(fxl::RefPtr<ExprEvalContext>& context,
                             EvalCallback cb) const {
  cb(Err("Unimplemented"), ExprValue());
}

void AddressOfExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ADDRESS_OF\n";
  expr_->Print(out, indent + 1);
}

void ArrayAccessExprNode::Eval(fxl::RefPtr<ExprEvalContext>& context,
                               EvalCallback cb) const {
  cb(Err("Unimplemented"), ExprValue());
}

void ArrayAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ARRAY_ACCESS\n";
  left_->Print(out, indent + 1);
  inner_->Print(out, indent + 1);
}

void DereferenceExprNode::Eval(fxl::RefPtr<ExprEvalContext>& context,
                               EvalCallback cb) const {
  cb(Err("Unimplemented"), ExprValue());
}

void DereferenceExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "DEREFERENCE\n";
  expr_->Print(out, indent + 1);
}

void IdentifierExprNode::Eval(fxl::RefPtr<ExprEvalContext>& context,
                              EvalCallback cb) const {
  context->GetVariable(name_.value(), std::move(cb));
}

void IdentifierExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "IDENTIFIER(" << name_.value() << ")\n";
}

void IntegerExprNode::Eval(fxl::RefPtr<ExprEvalContext>& context,
                           EvalCallback cb) const {
  cb(Err("Unimplemented"), ExprValue());
}

void IntegerExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "INTEGER(" << integer_.value() << ")\n";
}

void MemberAccessExprNode::Eval(fxl::RefPtr<ExprEvalContext>& context,
                                EvalCallback cb) const {
  cb(Err("Unimplemented"), ExprValue());
}

void MemberAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ACCESSOR(" << accessor_.value() << ")\n";
  left_->Print(out, indent + 1);
  out << IndentFor(indent + 1) << member_.value() << "\n";
}

void UnaryOpExprNode::Eval(fxl::RefPtr<ExprEvalContext>& context,
                           EvalCallback cb) const {
  expr_->Eval(context,
              [cb = std::move(cb), op = op_](const Err& err, ExprValue value) {
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
