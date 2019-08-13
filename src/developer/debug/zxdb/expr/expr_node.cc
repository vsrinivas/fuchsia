// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_node.h"

#include <stdlib.h>

#include <ostream>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/eval_operators.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/number_parser.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"
#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"
#include "src/developer/debug/zxdb/expr/resolve_array.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
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
         bt == BaseType::kBaseTypeSignedChar || bt == BaseType::kBaseTypeUnsigned ||
         bt == BaseType::kBaseTypeUnsignedChar;
}

}  // namespace

void ExprNode::EvalFollowReferences(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  Eval(context, [context, cb = std::move(cb)](const Err& err, ExprValue value) mutable {
    if (err.has_error())
      cb(err, ExprValue());
    else
      EnsureResolveReference(context, std::move(value),
                             ErrOrValue::FromPairCallback(std::move(cb)));
  });
}

void AddressOfExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  expr_->EvalFollowReferences(context, [cb = std::move(cb)](const Err& err,
                                                            ExprValue value) mutable {
    if (err.has_error()) {
      cb(err, ExprValue());
    } else if (value.source().type() != ExprValueSource::Type::kMemory) {
      cb(Err("Can't take the address of a temporary."), ExprValue());
    } else {
      // Construct a pointer type to the variable.
      auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, value.type_ref());

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

void ArrayAccessExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  left_->EvalFollowReferences(context, [inner = inner_, context, cb = std::move(cb)](
                                           const Err& err, ExprValue left_value) mutable {
    if (err.has_error()) {
      cb(err, ExprValue());
    } else {
      // "left" has been evaluated, now do "inner".
      inner->EvalFollowReferences(
          context, [context, left_value = std::move(left_value), cb = std::move(cb)](
                       const Err& err, ExprValue inner_value) mutable {
            if (err.has_error()) {
              cb(err, ExprValue());
            } else {
              // Both "left" and "inner" has been evaluated.
              int64_t offset = 0;
              Err offset_err = InnerValueToOffset(context, inner_value, &offset);
              if (offset_err.has_error()) {
                cb(offset_err, ExprValue());
              } else {
                ResolveArrayItem(std::move(context), std::move(left_value), offset,
                                 ErrOrValue::FromPairCallback(std::move(cb)));
              }
            }
          });
    }
  });
}

// static
Err ArrayAccessExprNode::InnerValueToOffset(fxl::RefPtr<EvalContext> context,
                                            const ExprValue& inner, int64_t* offset) {
  // Type should be some kind of number.
  const Type* abstract_type = inner.type();
  if (!abstract_type)
    return Err("Bad type, please file a bug with a repro.");

  // Skip "const", etc.
  fxl::RefPtr<Type> concrete_type = context->GetConcreteType(abstract_type);

  const BaseType* base_type = concrete_type->AsBaseType();
  if (!base_type || !BaseTypeCanBeArrayIndex(base_type))
    return Err("Bad type for array index.");

  // This uses signed integers to explicitly allow negative indexing which the user may want to do
  // for some reason.
  Err promote_err = inner.PromoteTo64(offset);
  if (promote_err.has_error())
    return promote_err;
  return Err();
}

void ArrayAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ARRAY_ACCESS\n";
  left_->Print(out, indent + 1);
  inner_->Print(out, indent + 1);
}

void BinaryOpExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  EvalBinaryOperator(std::move(context), left_, op_, right_,
                     ErrOrValue::FromPairCallback(std::move(cb)));
}

void BinaryOpExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "BINARY_OP(" + op_.value() + ")\n";
  left_->Print(out, indent + 1);
  right_->Print(out, indent + 1);
}

void CastExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  // Callback that does the cast given the right type of value.
  auto exec_cast = [context, cast_type = cast_type_, to_type = to_type_->type(),
                    cb = std::move(cb)](const Err& err, ExprValue value) mutable {
    if (err.has_error()) {
      cb(err, value);
    } else {
      ErrOrValue result = CastExprValue(context.get(), cast_type, value, to_type);
      if (result.has_error())
        cb(result.err(), ExprValue());
      else
        cb(Err(), result.take_value());
    }
  };

  from_->Eval(context, [context, cast_type = cast_type_, to_type = to_type_->type(),
                        exec_cast = std::move(exec_cast)](const Err& err, ExprValue value) mutable {
    // This lambda optionally follows the reference on the value according to the requirements of
    // the cast.
    if (err.has_error() || !CastShouldFollowReferences(context.get(), cast_type, value, to_type)) {
      exec_cast(err, value);  // Also handles the error cases.
    } else {
      EnsureResolveReference(context, std::move(value),
                             ErrOrValue::FromPairCallback(std::move(exec_cast)));
    }
  });
}

void CastExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "CAST(" << CastTypeToString(cast_type_) << ")\n";
  to_type_->Print(out, indent + 1);
  from_->Print(out, indent + 1);
}

void DereferenceExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  expr_->EvalFollowReferences(
      context, [context, cb = std::move(cb)](const Err& err, ExprValue value) mutable {
        // First check for pretty-printers for this type.
        if (PrettyType* pretty = context->GetPrettyTypeManager().GetForType(value.type())) {
          if (auto derefer = pretty->GetDereferencer()) {
            // The pretty type supplies dereference function.
            return derefer(context, value, std::move(cb));
          }
        }

        // Normal dereferencing operation.
        ResolvePointer(context, value, ErrOrValue::FromPairCallback(std::move(cb)));
      });
}

void DereferenceExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "DEREFERENCE\n";
  expr_->Print(out, indent + 1);
}

void FunctionCallExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  // Actually calling functions in the target is not supported.
  const char kNotSupportedMsg[] =
      "Arbitrary function calls are not supported. Only certain built-in getters will work.";
  if (!args_.empty())
    return cb(Err(kNotSupportedMsg), ExprValue());

  if (const MemberAccessExprNode* access = call_->AsMemberAccess()) {
    // Object member calls, check for getters provided by pretty-printers.
    std::string fn_name = access->member().GetFullName();
    access->left()->EvalFollowReferences(
        context, [context, cb = std::move(cb), op = access->accessor(), fn_name](
                     const Err& err, ExprValue value) mutable {
          if (err.has_error())
            return cb(err, ExprValue());

          if (op.type() == ExprTokenType::kArrow)
            EvalMemberPtrCall(context, value, fn_name, std::move(cb));
          else  // Assume ".".
            EvalMemberCall(context, value, fn_name, std::move(cb));
        });
    return;
  }

  cb(Err(kNotSupportedMsg), ExprValue());
}

void FunctionCallExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "FUNCTIONCALL\n";
  call_->Print(out, indent + 1);
  for (const auto& arg : args_)
    arg->Print(out, indent + 1);
}

// static
bool FunctionCallExprNode::IsValidCall(const fxl::RefPtr<ExprNode>& call) {
  return call && (call->AsIdentifier() || call->AsMemberAccess());
}

// static
void FunctionCallExprNode::EvalMemberCall(fxl::RefPtr<EvalContext> context, const ExprValue& object,
                                          const std::string& fn_name, EvalCallback cb) {
  if (!object.type())
    return cb(Err("No type information."), ExprValue());

  if (PrettyType* pretty = context->GetPrettyTypeManager().GetForType(object.type())) {
    // Have a PrettyType for the object type.
    if (auto getter = pretty->GetGetter(fn_name)) {
      return getter(context, object,
                    [type_name = object.type()->GetFullName(), fn_name, cb = std::move(cb)](
                        const Err& err, ExprValue value) mutable {
                      // This lambda exists just to rewrite the error message so it's clear the
                      // error is coming from the PrettyType and not the users's input. Otherwise
                      // it can look quite confusing.
                      if (err.has_error()) {
                        cb(Err("When evaluating the internal pretty getter '%s()' on the type:\n  "
                               "%s\nGot the error:\n  %s\nPlease file a bug.",
                               fn_name.c_str(), type_name.c_str(), err.msg().c_str()),
                           ExprValue());
                      } else {
                        cb(Err(), std::move(value));
                      }
                    });
    }
  }

  cb(Err("No built-in getter '%s()' for the type\n  %s", fn_name.c_str(),
         object.type()->GetFullName().c_str()),
     ExprValue());
}

// static
void FunctionCallExprNode::EvalMemberPtrCall(fxl::RefPtr<EvalContext> context,
                                             const ExprValue& object_ptr,
                                             const std::string& fn_name, EvalCallback cb) {
  // Callback executed on the object once the pointer has been dereferenced.
  auto on_pointer_resolved = [context, fn_name, cb = std::move(cb)](const Err& err,
                                                                    ExprValue value) mutable {
    if (err.has_error())
      cb(err, ExprValue());
    else
      EvalMemberCall(std::move(context), value, fn_name, std::move(cb));
  };

  // The base object could itself have a dereference operator. For example, if you have a:
  //   std::unique_ptr<std::vector<int>> foo;
  // and do:
  //   foo->size()
  // It needs to use the pretty dereferencer on foo before trying to access the size() function
  // on the resulting object.
  if (PrettyType* pretty = context->GetPrettyTypeManager().GetForType(object_ptr.type())) {
    if (auto derefer = pretty->GetDereferencer()) {
      // The pretty type supplies dereference function.
      return derefer(context, object_ptr, std::move(on_pointer_resolved));
    }
  }

  // Regular, assume the base is a pointer.
  ResolvePointer(context, object_ptr, ErrOrValue::FromPairCallback(std::move(on_pointer_resolved)));
}

void IdentifierExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  context->GetNamedValue(
      ident_, [cb = std::move(cb)](const Err& err, fxl::RefPtr<Symbol>, ExprValue value) mutable {
        // Discard resolved symbol, we only need the value.
        cb(err, std::move(value));
      });
}

void IdentifierExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "IDENTIFIER(" << ident_.GetDebugName() << ")\n";
}

void LiteralExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
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

void MemberAccessExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  bool is_arrow = accessor_.type() == ExprTokenType::kArrow;
  left_->EvalFollowReferences(context, [context, is_arrow, member = member_, cb = std::move(cb)](
                                           const Err& err, ExprValue base) mutable {
    if (!is_arrow) {
      // "." operator.
      ErrOrValue result = ResolveMember(context, base, member);
      cb(result.err_or_empty(), std::move(result.take_value_or_empty()));
      return;
    }

    // Everything else should be a -> operator.

    if (PrettyType* pretty = context->GetPrettyTypeManager().GetForType(base.type())) {
      if (auto derefer = pretty->GetDereferencer()) {
        // The pretty type supplies dereference function. This turns foo->bar into deref(foo).bar.
        return derefer(
            context, base,
            [context, member, cb = std::move(cb)](const Err& err, ExprValue non_ptr_base) mutable {
              ErrOrValue result = ResolveMember(context, non_ptr_base, member);
              cb(result.err_or_empty(), std::move(result.take_value_or_empty()));
            });
      }
    }

    // Normal collection resolution.
    ResolveMemberByPointer(context, base, member,
                           [cb = std::move(cb)](ErrOrValue result, fxl::RefPtr<Symbol>) mutable {
                             // Discard resolved symbol, we only need the value.
                             cb(result.err_or_empty(), std::move(result.take_value_or_empty()));
                           });
  });
}

void MemberAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ACCESSOR(" << accessor_.value() << ")\n";
  left_->Print(out, indent + 1);
  out << IndentFor(indent + 1) << member_.GetFullName() << "\n";
}

void SizeofExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  if (const TypeExprNode* type_node = const_cast<ExprNode*>(expr_.get())->AsType()) {
    // Types just get used directly.
    SizeofType(context, type_node->type().get(), std::move(cb));
  } else {
    // Everything else gets evaluated. Strictly C++ won't do this because it's statically typed, but
    // our expression system is not. This doesn't need to follow references because we only need the
    // type and the
    expr_->Eval(context, [context, cb = std::move(cb)](const Err& err, ExprValue value) mutable {
      if (err.has_error())
        cb(err, ExprValue());
      else
        SizeofType(context, value.type(), std::move(cb));
    });
  }
}

void SizeofExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "SIZEOF\n";
  expr_->Print(out, indent + 1);
}

// static
void SizeofExprNode::SizeofType(fxl::RefPtr<EvalContext> context, const Type* in_type,
                                EvalCallback cb) {
  // References should get stripped (sizeof(char&) = 1).
  if (!in_type) {
    cb(Err("Can't do sizeof on a null type."), ExprValue());
    return;
  }

  fxl::RefPtr<Type> type = context->GetConcreteType(in_type);
  if (type->is_declaration()) {
    cb(Err("Can't resolve forward declaration for '%s'.", in_type->GetFullName().c_str()),
       ExprValue());
    return;
  }

  if (DwarfTagIsEitherReference(type->tag()))
    type = RefPtrTo(type->AsModifiedType()->modified().Get()->AsType());
  if (!type) {
    cb(Err("Symbol error for '%s'.", in_type->GetFullName().c_str()), ExprValue());
    return;
  }

  cb(Err(), ExprValue(type->byte_size()));
}

void TypeExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  // Doesn't make sense to evaluate a type, callers like casts that expect a type name will look
  // into the node themselves.
  cb(Err("Can not evaluate a type name."), ExprValue());
}

void TypeExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "TYPE(";
  if (type_)
    out << type_->GetFullName();
  out << ")\n";
}

void UnaryOpExprNode::Eval(fxl::RefPtr<EvalContext> context, EvalCallback cb) const {
  expr_->EvalFollowReferences(
      context, [cb = std::move(cb), op = op_](const Err& err, ExprValue value) mutable {
        if (err.has_error())
          cb(err, std::move(value));
        else
          EvalUnaryOperator(op, value, ErrOrValue::FromPairCallback(std::move(cb)));
      });
}

void UnaryOpExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "UNARY(" << op_.value() << ")\n";
  expr_->Print(out, indent + 1);
}

}  // namespace zxdb
