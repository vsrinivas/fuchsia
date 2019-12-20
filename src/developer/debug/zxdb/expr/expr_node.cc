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
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/symbol_utils.h"
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

void DoResolveConcreteMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                             const ParsedIdentifier& member, EvalCallback cb) {
  if (PrettyType* pretty = context->GetPrettyTypeManager().GetForType(value.type())) {
    if (auto getter = pretty->GetMember(member.GetFullName())) {
      return getter(context, value, std::move(cb));
    }
  }

  return ResolveMember(context, value, member, std::move(cb));
}

}  // namespace

void ExprNode::EvalFollowReferences(const fxl::RefPtr<EvalContext>& context,
                                    EvalCallback cb) const {
  Eval(context, [context, cb = std::move(cb)](ErrOrValue value) mutable {
    if (value.has_error())
      return cb(value);
    EnsureResolveReference(context, std::move(value.value()), std::move(cb));
  });
}

void AddressOfExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  expr_->EvalFollowReferences(context, [cb = std::move(cb)](ErrOrValue value) mutable {
    if (value.has_error()) {
      cb(value);
    } else if (value.value().source().type() != ExprValueSource::Type::kMemory) {
      cb(Err("Can't take the address of a temporary."));
    } else if (value.value().source().bit_size() != 0) {
      cb(Err("Can't take the address of a bitfield."));
    } else {
      // Construct a pointer type to the variable.
      auto ptr_type =
          fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, value.value().type_ref());

      std::vector<uint8_t> contents;
      contents.resize(kTargetPointerSize);
      TargetPointer address = value.value().source().address();
      memcpy(&contents[0], &address, sizeof(kTargetPointerSize));

      cb(ExprValue(std::move(ptr_type), std::move(contents)));
    }
  });
}

void AddressOfExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ADDRESS_OF\n";
  expr_->Print(out, indent + 1);
}

void ArrayAccessExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  left_->EvalFollowReferences(context, [inner = inner_, context,
                                        cb = std::move(cb)](ErrOrValue left_value) mutable {
    if (left_value.has_error()) {
      cb(left_value);
    } else {
      // "left" has been evaluated, now do "inner".
      inner->EvalFollowReferences(context, [context, left_value = left_value.take_value(),
                                            cb = std::move(cb)](ErrOrValue inner_value) mutable {
        if (inner_value.has_error()) {
          cb(inner_value);
        } else {
          // Both "left" and "inner" has been evaluated.
          int64_t offset = 0;
          if (Err err = InnerValueToOffset(context, inner_value.value(), &offset); err.has_error())
            cb(err);
          else
            ResolveArrayItem(std::move(context), std::move(left_value), offset, std::move(cb));
        }
      });
    }
  });
}

// static
Err ArrayAccessExprNode::InnerValueToOffset(const fxl::RefPtr<EvalContext>& context,
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

void BinaryOpExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  EvalBinaryOperator(std::move(context), left_, op_, right_, std::move(cb));
}

void BinaryOpExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "BINARY_OP(" + op_.value() + ")\n";
  left_->Print(out, indent + 1);
  right_->Print(out, indent + 1);
}

void CastExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  from_->Eval(context, [context, cast_type = cast_type_, to_type = to_type_->type(),
                        cb = std::move(cb)](ErrOrValue value) mutable {
    if (value.has_error())
      cb(value);
    else
      CastExprValue(context, cast_type, value.value(), to_type, ExprValueSource(), std::move(cb));
  });
}

void CastExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "CAST(" << CastTypeToString(cast_type_) << ")\n";
  to_type_->Print(out, indent + 1);
  from_->Print(out, indent + 1);
}

void DereferenceExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  expr_->EvalFollowReferences(context, [context, cb = std::move(cb)](ErrOrValue value) mutable {
    if (value.has_error())
      return cb(std::move(value));

    // First check for pretty-printers for this type.
    if (PrettyType* pretty = context->GetPrettyTypeManager().GetForType(value.value().type())) {
      if (auto derefer = pretty->GetDereferencer()) {
        // The pretty type supplies dereference function.
        return derefer(context, value.value(), std::move(cb));
      }
    }

    // Normal dereferencing operation.
    ResolvePointer(context, value.value(), std::move(cb));
  });
}

void DereferenceExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "DEREFERENCE\n";
  expr_->Print(out, indent + 1);
}

void FunctionCallExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  // Actually calling functions in the target is not supported.
  const char kNotSupportedMsg[] =
      "Arbitrary function calls are not supported. Only certain built-in getters will work.";
  if (!args_.empty())
    return cb(Err(kNotSupportedMsg));

  if (const MemberAccessExprNode* access = call_->AsMemberAccess()) {
    // Object member calls, check for getters provided by pretty-printers.
    std::string fn_name = access->member().GetFullName();
    access->left()->EvalFollowReferences(
        context,
        [context, cb = std::move(cb), op = access->accessor(), fn_name](ErrOrValue value) mutable {
          if (value.has_error())
            return cb(value);

          if (op.type() == ExprTokenType::kArrow)
            EvalMemberPtrCall(context, value.value(), fn_name, std::move(cb));
          else  // Assume ".".
            EvalMemberCall(context, value.value(), fn_name, std::move(cb));
        });
    return;
  }

  cb(Err(kNotSupportedMsg));
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
void FunctionCallExprNode::EvalMemberCall(const fxl::RefPtr<EvalContext>& context,
                                          const ExprValue& object, const std::string& fn_name,
                                          EvalCallback cb) {
  if (!object.type())
    return cb(Err("No type information."));

  if (PrettyType* pretty = context->GetPrettyTypeManager().GetForType(object.type())) {
    // Have a PrettyType for the object type.
    if (auto getter = pretty->GetGetter(fn_name)) {
      return getter(context, object,
                    [type_name = object.type()->GetFullName(), fn_name,
                     cb = std::move(cb)](ErrOrValue value) mutable {
                      // This lambda exists just to rewrite the error message so it's clear the
                      // error is coming from the PrettyType and not the users's input. Otherwise
                      // it can look quite confusing.
                      if (value.has_error()) {
                        cb(
                            Err("When evaluating the internal pretty getter '%s()' on the type:\n  "
                                "%s\nGot the error:\n  %s\nPlease file a bug.",
                                fn_name.c_str(), type_name.c_str(), value.err().msg().c_str()));
                      } else {
                        cb(std::move(value));
                      }
                    });
    }
  }

  cb(Err("No built-in getter '%s()' for the type\n  %s", fn_name.c_str(),
         object.type()->GetFullName().c_str()));
}

// static
void FunctionCallExprNode::EvalMemberPtrCall(const fxl::RefPtr<EvalContext>& context,
                                             const ExprValue& object_ptr,
                                             const std::string& fn_name, EvalCallback cb) {
  // Callback executed on the object once the pointer has been dereferenced.
  auto on_pointer_resolved = [context, fn_name, cb = std::move(cb)](ErrOrValue value) mutable {
    if (value.has_error())
      cb(value);
    else
      EvalMemberCall(std::move(context), value.value(), fn_name, std::move(cb));
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
  ResolvePointer(context, object_ptr, std::move(on_pointer_resolved));
}

void IdentifierExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  context->GetNamedValue(ident_, std::move(cb));
}

void IdentifierExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "IDENTIFIER(" << ident_.GetDebugName() << ")\n";
}

void LiteralExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  switch (token_.type()) {
    case ExprTokenType::kInteger: {
      cb(StringToNumber(token_.value()));
      break;
    }
    case ExprTokenType::kFloat: {
      cb(ValueForFloatToken(context->GetLanguage(), token_));
      break;
    }
    case ExprTokenType::kStringLiteral: {
      // Include the null terminator in the string array as C would.
      std::vector<uint8_t> string_as_array;
      string_as_array.reserve(token_.value().size() + 1);
      string_as_array.assign(token_.value().begin(), token_.value().end());
      string_as_array.push_back(0);
      cb(ExprValue(MakeStringLiteralType(token_.value().size() + 1), std::move(string_as_array)));
      break;
    }
    case ExprTokenType::kTrue: {
      cb(ExprValue(true));
      break;
    }
    case ExprTokenType::kFalse: {
      cb(ExprValue(false));
      break;
    }
    default:
      FXL_NOTREACHED();
  }
}

void LiteralExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "LITERAL(" << token_.value() << ")\n";
}

void MemberAccessExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  bool by_pointer = accessor_.type() == ExprTokenType::kArrow;
  left_->EvalFollowReferences(context, [context, by_pointer, member = member_,
                                        cb = std::move(cb)](ErrOrValue base) mutable {
    if (base.has_error())
      return cb(base);

    auto base_value = base.value();

    // Rust references can be accessed with '.'
    if (!by_pointer) {
      fxl::RefPtr<Type> concrete_base = base_value.GetConcreteType(context.get());

      if (!concrete_base || concrete_base->tag() != DwarfTag::kPointerType ||
          concrete_base->GetLanguage() != DwarfLang::kRust ||
          concrete_base->GetAssignedName().substr(0, 1) != "&") {
        return DoResolveConcreteMember(context, base_value, member, std::move(cb));
      }
    }

    PrettyType::EvalFunction getter = [member](const fxl::RefPtr<EvalContext>& context,
                                               const ExprValue& value, EvalCallback cb) {
      DoResolveConcreteMember(context, value, member, std::move(cb));
    };
    PrettyType::EvalFunction derefer = ResolvePointer;

    if (PrettyType* pretty = context->GetPrettyTypeManager().GetForType(base_value.type())) {
      derefer = pretty->GetDereferencer();
    } else {
      fxl::RefPtr<Collection> coll;
      if (Err err = GetConcretePointedToCollection(context, base_value.type(), &coll);
          err.has_error()) {
        return cb(err);
      }

      if (PrettyType* pretty = context->GetPrettyTypeManager().GetForType(coll.get())) {
        getter = pretty->GetMember(member.GetFullName());
      } else {
        getter = nullptr;
      }
    }

    if (getter && derefer) {
      return derefer(context, base_value,
                     [context, member, getter = std::move(getter),
                      cb = std::move(cb)](ErrOrValue non_ptr_base) mutable {
                       if (non_ptr_base.has_error())
                         return cb(non_ptr_base);
                       getter(context, non_ptr_base.value(), std::move(cb));
                     });
    }

    // Normal collection resolution.
    ResolveMemberByPointer(context, base.value(), member,
                           [cb = std::move(cb)](ErrOrValue result, const FoundMember&) mutable {
                             // Discard resolved symbol, we only need the value.
                             cb(std::move(result));
                           });
  });
}

void MemberAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ACCESSOR(" << accessor_.value() << ")\n";
  left_->Print(out, indent + 1);
  out << IndentFor(indent + 1) << member_.GetFullName() << "\n";
}

void SizeofExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  if (const TypeExprNode* type_node = const_cast<ExprNode*>(expr_.get())->AsType()) {
    // Types just get used directly.
    SizeofType(context, type_node->type().get(), std::move(cb));
  } else {
    // Everything else gets evaluated. Strictly C++ won't do this because it's statically typed, but
    // our expression system is not. This doesn't need to follow references because we only need the
    // type and the
    expr_->Eval(context, [context, cb = std::move(cb)](ErrOrValue value) mutable {
      if (value.has_error())
        return cb(value);
      SizeofType(context, value.value().type(), std::move(cb));
    });
  }
}

void SizeofExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "SIZEOF\n";
  expr_->Print(out, indent + 1);
}

// static
void SizeofExprNode::SizeofType(const fxl::RefPtr<EvalContext>& context, const Type* in_type,
                                EvalCallback cb) {
  // References should get stripped (sizeof(char&) = 1).
  if (!in_type)
    return cb(Err("Can't do sizeof on a null type."));

  fxl::RefPtr<Type> type = context->GetConcreteType(in_type);
  if (type->is_declaration())
    return cb(Err("Can't resolve forward declaration for '%s'.", in_type->GetFullName().c_str()));

  if (DwarfTagIsEitherReference(type->tag()))
    type = RefPtrTo(type->AsModifiedType()->modified().Get()->AsType());
  if (!type)
    return cb(Err("Symbol error for '%s'.", in_type->GetFullName().c_str()));

  cb(ExprValue(type->byte_size()));
}

void TypeExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  // Doesn't make sense to evaluate a type, callers like casts that expect a type name will look
  // into the node themselves.
  cb(Err("Can not evaluate a type name."));
}

void TypeExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "TYPE(";
  if (type_)
    out << type_->GetFullName();
  out << ")\n";
}

void UnaryOpExprNode::Eval(const fxl::RefPtr<EvalContext>& context, EvalCallback cb) const {
  expr_->EvalFollowReferences(context,
                              [context, cb = std::move(cb), op = op_](ErrOrValue value) mutable {
                                if (value.has_error())
                                  cb(value);
                                else
                                  EvalUnaryOperator(context, op, value.value(), std::move(cb));
                              });
}

void UnaryOpExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "UNARY(" << op_.value() << ")\n";
  expr_->Print(out, indent + 1);
}

}  // namespace zxdb
