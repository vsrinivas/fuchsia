// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_node.h"

#include <stdlib.h>

#include <ostream>

#include "src/developer/debug/shared/message_loop.h"
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
#include "src/developer/debug/zxdb/expr/vm_stream.h"
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

// Prints the expression, or if null, ";".
void PrintExprOrSemicolon(std::ostream& out, int indent, const fxl::RefPtr<ExprNode>& expr) {
  if (expr) {
    expr->Print(out, indent);
  } else {
    out << IndentFor(indent) << ";\n";
  }
}

}  // namespace

void ExprNode::EmitBytecodeExpandRef(VmStream& stream) const {
  EmitBytecode(stream);
  stream.push_back(VmOp::MakeExpandRef());
}

void AddressOfExprNode::EmitBytecode(VmStream& stream) const {
  expr_->EmitBytecodeExpandRef(stream);
  stream.push_back(VmOp::MakeCallback1(
      [](const fxl::RefPtr<EvalContext>& eval_context, ExprValue value) -> ErrOrValue {
        if (value.source().type() != ExprValueSource::Type::kMemory)
          return Err("Can't take the address of a temporary.");
        if (value.source().bit_size() != 0)
          return Err("Can't take the address of a bitfield.");

        // Construct a pointer type to the variable.
        auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, value.type_ref());
        TargetPointer address = value.source().address();
        return ExprValue(address, std::move(ptr_type));
      }));
}

void AddressOfExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ADDRESS_OF\n";
  expr_->Print(out, indent + 1);
}

void ArrayAccessExprNode::EmitBytecode(VmStream& stream) const {
  left_->EmitBytecodeExpandRef(stream);
  inner_->EmitBytecodeExpandRef(stream);

  stream.push_back(
      VmOp::MakeAsyncCallback2([](const fxl::RefPtr<EvalContext>& context, ExprValue left,
                                  ExprValue inner, EvalCallback cb) mutable {
        // Both "left" and "inner" has been evaluated.
        int64_t offset = 0;
        if (Err err = InnerValueToOffset(context, inner, &offset); err.has_error()) {
          cb(err);
        } else {
          ResolveArrayItem(context, left, offset, std::move(cb));
        }
      }));
}

// static
Err ArrayAccessExprNode::InnerValueToOffset(const fxl::RefPtr<EvalContext>& context,
                                            const ExprValue& inner, int64_t* offset) {
  // Skip "const", etc.
  fxl::RefPtr<BaseType> base_type = context->GetConcreteTypeAs<BaseType>(inner.type());
  if (!base_type || !BaseTypeCanBeArrayIndex(base_type.get()))
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

void BinaryOpExprNode::EmitBytecode(VmStream& stream) const {
  // Need to implement short-circuiting evaluation for || and && to avoid unwanted side-effects.
  left_->EmitBytecodeExpandRef(stream);
  if (op_.type() == ExprTokenType::kLogicalOr) {
    // Emit the equivalent of: "left ? true : (right ? true : false)".
    VmBytecodeForwardJumpIfFalse jump_to_right(&stream);  // -> RIGHT

    // Left is true, emit a "true" value and jump to the end.
    stream.push_back(VmOp::MakeLiteral(ExprValue(true)));
    VmBytecodeForwardJump left_jump_out(&stream);  // -> END

    // RIGHT: Evaluate right side. The first condition jump goes here.
    jump_to_right.JumpToHere();
    right_->EmitBytecodeExpandRef(stream);

    // On false, jump to the end (after the "then").
    VmBytecodeForwardJumpIfFalse final_cond_jump(&stream);  // -> FALSE

    // Right is true, emit a "true" value and jump to the end.
    stream.push_back(VmOp::MakeLiteral(ExprValue(true)));
    VmBytecodeForwardJump right_jump_out(&stream);  // -> END

    // FALSE: Condition is false.
    final_cond_jump.JumpToHere();
    stream.push_back(VmOp::MakeLiteral(ExprValue(false)));

    // END: End of condition, all the done jumps end up here.
    left_jump_out.JumpToHere();
    right_jump_out.JumpToHere();
  } else if (op_.type() == ExprTokenType::kDoubleAnd) {
    VmBytecodeForwardJumpIfFalse left_jump_to_false(&stream);  // -> FALSE

    // Left was true, now evaluate right.
    right_->EmitBytecodeExpandRef(stream);
    VmBytecodeForwardJumpIfFalse right_jump_to_false(&stream);  // -> FALSE

    // True case.
    stream.push_back(VmOp::MakeLiteral(ExprValue(true)));
    VmBytecodeForwardJump jump_to_end(&stream);  // -> END

    // FALSE: The failure cases end up here.
    left_jump_to_false.JumpToHere();
    right_jump_to_false.JumpToHere();
    stream.push_back(VmOp::MakeLiteral(ExprValue(false)));

    // END:
    jump_to_end.JumpToHere();
  } else {
    // All other binary operators can be evaluated directly.
    right_->EmitBytecode(stream);
    stream.push_back(VmOp::MakeBinary(op_));
  }
}

void BinaryOpExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "BINARY_OP(" + op_.value() + ")\n";
  left_->Print(out, indent + 1);
  right_->Print(out, indent + 1);
}

void BlockExprNode::EmitBytecode(VmStream& stream) const {
  // All nodes must evaluate to some value. We define the block's value as being that of the
  // last expression (like Rust with no semicolon), and an empty ExprValue if there is nothing in
  // the block.
  if (statements_.empty()) {
    stream.push_back(VmOp::MakeLiteral(ExprValue()));
    return;
  }

  for (size_t i = 0; i < statements_.size() - 1; i++) {
    statements_[i]->EmitBytecode(stream);
    stream.push_back(VmOp::MakeDrop());  // Discard intermediate statement results.
  }
  statements_.back()->EmitBytecode(stream);

  // Clean up any locals. This removes any variables beyond what were in scope when the block
  // entered. See "Local variables" in vm_op.h for more info.
  if (entry_local_var_count_)
    stream.push_back(VmOp::MakePopLocals(*entry_local_var_count_));
}

void BlockExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "BLOCK\n";
  for (const auto& stmt : statements_)
    stmt->Print(out, indent + 1);
}

void CastExprNode::EmitBytecode(VmStream& stream) const {
  from_->EmitBytecode(stream);
  // Uses the non-concrete type for the "to" type because the cast will internally get the
  // concrete type, but will preserve the original type in the output so that the result will have
  // the same type the user typed (like a typedef name).
  stream.push_back(VmOp::MakeAsyncCallback1(
      [cast_type = cast_type_, to_type = to_type_->type()](
          const fxl::RefPtr<EvalContext>& eval_context, ExprValue from, EvalCallback cb) mutable {
        CastExprValue(eval_context, cast_type, from, to_type, ExprValueSource(), std::move(cb));
      }));
}

void CastExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "CAST(" << CastTypeToString(cast_type_) << ")\n";
  to_type_->Print(out, indent + 1);
  from_->Print(out, indent + 1);
}

void ConditionExprNode::EmitBytecode(VmStream& stream) const {
  // Instruction indices of all jumps that need to go to the end of the if statement.
  std::vector<size_t> done_jumps;

  for (const Pair& pair : conds_) {
    pair.cond->EmitBytecodeExpandRef(stream);

    // Jump over the "then" case if false.
    VmBytecodeForwardJumpIfFalse jump_to_next(&stream);

    pair.then->EmitBytecode(stream);

    // Jump to the end of the entire if/else block (we don't know the dest until the bottom).
    done_jumps.push_back(stream.size());
    stream.push_back(VmOp::MakeJump());

    // Fixup the "else" case to go to here.
    jump_to_next.JumpToHere();
  }

  if (else_) {
    else_->EmitBytecode(stream);
  } else {
    // When there is no explicit "else" case, the if expression still needs a result.
    stream.push_back(VmOp::MakeLiteral(ExprValue()));
  }

  // Fixup all previous jumps to the end of the blocks.
  for (size_t jump_source : done_jumps) {
    stream[jump_source].SetJumpDest(static_cast<uint32_t>(stream.size()));
  }
}

void ConditionExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "CONDITION\n";
  for (size_t i = 0; i < conds_.size(); i++) {
    if (i == 0)
      out << IndentFor(indent + 1) << "IF\n";
    else
      out << IndentFor(indent + 1) << "ELSEIF\n";
    conds_[i].cond->Print(out, indent + 2);

    if (conds_[i].then) {
      out << IndentFor(indent + 1) << "THEN\n";
      conds_[i].then->Print(out, indent + 2);
    }
  }
  if (else_) {
    out << IndentFor(indent + 1) << "ELSE\n";
    else_->Print(out, indent + 2);
  }
}

void DereferenceExprNode::EmitBytecode(VmStream& stream) const {
  expr_->EmitBytecodeExpandRef(stream);

  stream.push_back(VmOp::MakeAsyncCallback1(
      [](const fxl::RefPtr<EvalContext>& eval_context, ExprValue value, EvalCallback cb) mutable {
        // First check for pretty-printers for this type.
        if (PrettyType* pretty = eval_context->GetPrettyTypeManager().GetForType(value.type())) {
          if (auto derefer = pretty->GetDereferencer()) {
            // The pretty type supplies dereference function.
            return derefer(eval_context, value, std::move(cb));
          }
        }

        // Normal dereferencing operation.
        ResolvePointer(eval_context, value, std::move(cb));
      }));
}

void DereferenceExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "DEREFERENCE\n";
  expr_->Print(out, indent + 1);
}

void FunctionCallExprNode::EmitBytecode(VmStream& stream) const {
  // Start with all parameters on the stack.
  for (const auto& arg : args_) {
    arg->EmitBytecode(stream);
  }

  if (const MemberAccessExprNode* access = call_->AsMemberAccess()) {
    // For member calls, we also need to evaluate the object. That will appear as the last
    // "parameter".
    access->left()->EmitBytecodeExpandRef(stream);
    stream.push_back(VmOp::MakeAsyncCallbackN(
        static_cast<int>(args_.size()) + 1,
        [fn_name = access->member().GetFullName(), op = access->accessor()](
            const fxl::RefPtr<EvalContext>& eval_context, std::vector<ExprValue> params_and_object,
            EvalCallback cb) {
          // The last parameter is the object, extract it.
          ExprValue object = std::move(params_and_object.back());
          params_and_object.pop_back();

          if (params_and_object.size() != 0) {
            // Currently we do not support any parameters. This can be handled in the future if
            // needed.
            return cb(
                Err("Arbitrary function calls are not supported. Only certain built-in getters "
                    "will work."));
          }

          if (op.type() == ExprTokenType::kArrow) {
            EvalMemberPtrCall(eval_context, object, fn_name, std::move(cb));
          } else {  // Assume ".".
            EvalMemberCall(eval_context, object, fn_name, std::move(cb));
          }
        }));
  } else if (const IdentifierExprNode* ident = call_->AsIdentifier()) {
    // Simple standalone function call.
    stream.push_back(VmOp::MakeAsyncCallbackN(
        static_cast<int>(args_.size()),
        [fn_name = ident->ident()](const fxl::RefPtr<EvalContext>& eval_context,
                                   const std::vector<ExprValue>& params, EvalCallback cb) {
          // If we support standalone function calls (either for builtin stuff or for calling
          // functions in the program, that implementation goes here.
          (void)fn_name;  // Will be needed when we implement more.
          cb(
              Err("Arbitrary function calls are not supported. Only certain built-in getters will "
                  "work."));
        }));
  } else {
    stream.push_back(VmOp::MakeError(Err("Unknown function call type.")));
  }
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

void IdentifierExprNode::EmitBytecode(VmStream& stream) const {
  stream.push_back(VmOp::MakeAsyncCallback0(
      [ident = ident_](const fxl::RefPtr<EvalContext>& exec_context, EvalCallback cb) mutable {
        exec_context->GetNamedValue(ident, std::move(cb));
      }));
}

void IdentifierExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "IDENTIFIER(" << ident_.GetDebugName() << ")\n";
}

void LiteralExprNode::EmitBytecode(VmStream& stream) const {
  ErrOrValue literal((ExprValue()));
  switch (token_.type()) {
    case ExprTokenType::kInteger: {
      literal = StringToNumber(language_, token_.value());
      break;
    }
    case ExprTokenType::kFloat: {
      literal = ValueForFloatToken(language_, token_);
      break;
    }
    case ExprTokenType::kStringLiteral: {
      // Include the null terminator in the string array as C would.
      std::vector<uint8_t> string_as_array;
      string_as_array.reserve(token_.value().size() + 1);
      string_as_array.assign(token_.value().begin(), token_.value().end());
      string_as_array.push_back(0);
      literal =
          ExprValue(MakeStringLiteralType(token_.value().size() + 1), std::move(string_as_array));
      break;
    }
    case ExprTokenType::kCharLiteral: {
      FX_DCHECK(token_.value().size() == 1);
      switch (language_) {
        case ExprLanguage::kC: {
          int8_t value8 = token_.value()[0];
          literal = ExprValue(
              value8, fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 1, "char"));
          break;
        }
        case ExprLanguage::kRust: {
          // Rust character literals are 32-bit unsigned words even though we only support 8-bit for
          // now. Promote to 32-bits.
          uint32_t value32 = token_.value()[0];
          literal = ExprValue(
              value32, fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsignedChar, 4, "char"));
          break;
        }
      }
      break;
    }
    case ExprTokenType::kTrue: {
      literal = ExprValue(true);
      break;
    }
    case ExprTokenType::kFalse: {
      literal = ExprValue(false);
      break;
    }
    default:
      FX_NOTREACHED();
  }
  if (literal.has_error()) {
    stream.push_back(VmOp::MakeError(literal.err(), token_));
  } else {
    stream.push_back(VmOp::MakeLiteral(literal.value(), token_));
  }
}

void LiteralExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "LITERAL(" << token_.value() << ")\n";
}

void LocalVarExprNode::EmitBytecode(VmStream& stream) const {
  stream.push_back(VmOp::MakeGetLocal(slot_));
}

void LocalVarExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "LOCAL_VAR(" << slot_ << ")\n";
}

void LoopExprNode::EmitBytecode(VmStream& stream) const {
  if (init_) {
    init_->EmitBytecode(stream);
    stream.push_back(VmOp::MakeDrop());  // The result of the initialization expression is ignored.
  }

  // Top of actual loop contents.
  uint32_t loop_top = stream.size();

  VmBytecodeForwardJumpIfFalse precondition_jumper;
  if (precondition_) {
    precondition_->EmitBytecode(stream);
    precondition_jumper.SetSource(&stream);  // Jump out of loop if precondition is false.
  }

  if (contents_) {
    contents_->EmitBytecode(stream);
    stream.push_back(VmOp::MakeDrop());  // The result of the contents is ignored.
  }

  VmBytecodeForwardJumpIfFalse postcondition_jumper;
  if (postcondition_) {
    postcondition_->EmitBytecode(stream);
    postcondition_jumper.SetSource(&stream);  // Jump out of loop if postcondition is false.
  }

  if (incr_) {
    incr_->EmitBytecode(stream);
    stream.push_back(VmOp::MakeDrop());  // The result of the increment expression is ignored.
  }

  // Jump back to the top of the loop.
  stream.push_back(VmOp::MakeJump(loop_top));

  // The end of the loop (these will do nothing if we never called SetSource() on them).
  precondition_jumper.JumpToHere();
  postcondition_jumper.JumpToHere();

  // Clean up any locals. This removes any variables beyond what were in scope when the init
  // expression started. See "Local variables" in vm_op.h for more info.
  if (init_local_var_count_)
    stream.push_back(VmOp::MakePopLocals(*init_local_var_count_));

  // Push the result of the loop expression (no value).
  stream.push_back(VmOp::MakeLiteral(ExprValue()));
}

void LoopExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "LOOP(" << token_.value() << ")\n";
  PrintExprOrSemicolon(out, indent + 1, init_);
  PrintExprOrSemicolon(out, indent + 1, precondition_);
  PrintExprOrSemicolon(out, indent + 1, postcondition_);
  PrintExprOrSemicolon(out, indent + 1, incr_);
  PrintExprOrSemicolon(out, indent + 1, contents_);
}

void MemberAccessExprNode::EmitBytecode(VmStream& stream) const {
  left_->EmitBytecodeExpandRef(stream);

  bool by_pointer = accessor_.type() == ExprTokenType::kArrow;
  stream.push_back(VmOp::MakeAsyncCallback1(
      [by_pointer, member = member_](const fxl::RefPtr<EvalContext>& context, ExprValue base_value,
                                     EvalCallback cb) mutable {
        // Rust references can be accessed with '.'
        if (!by_pointer) {
          fxl::RefPtr<Type> concrete_base = context->GetConcreteType(base_value.type());

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
        ResolveMemberByPointer(context, base_value, member,
                               [cb = std::move(cb)](ErrOrValue result, const FoundMember&) mutable {
                                 // Discard resolved symbol, we only need the value.
                                 cb(std::move(result));
                               });
      }));
}

void MemberAccessExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ACCESSOR(" << accessor_.value() << ")\n";
  left_->Print(out, indent + 1);
  out << IndentFor(indent + 1) << member_.GetFullName() << "\n";
}

void SizeofExprNode::EmitBytecode(VmStream& stream) const {
  if (const TypeExprNode* type_node = const_cast<ExprNode*>(expr_.get())->AsType()) {
    // Ask for the size of the type at execution time (it needs the EvalContext for everything).
    stream.push_back(VmOp::MakeCallback0(
        [type = type_node->type()](const fxl::RefPtr<EvalContext>& eval_context) -> ErrOrValue {
          return SizeofType(eval_context, type.get());
        }));
  } else {
    // Everything else gets evaluated. Strictly C++ won't do this because it's statically typed, but
    // our expression system is not. This doesn't need to follow references because we only need the
    // type and SizeofType() follows them as needed.
    expr_->EmitBytecodeExpandRef(stream);
    stream.push_back(VmOp::MakeCallback1(
        [](const fxl::RefPtr<EvalContext>& eval_context, ExprValue param) -> ErrOrValue {
          return SizeofType(eval_context, param.type());
        }));
  }
}

void SizeofExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "SIZEOF\n";
  expr_->Print(out, indent + 1);
}

// static
ErrOrValue SizeofExprNode::SizeofType(const fxl::RefPtr<EvalContext>& context,
                                      const Type* in_type) {
  // References should get stripped (sizeof(char&) = 1).
  if (!in_type)
    return Err("Can't do sizeof on a null type.");

  fxl::RefPtr<Type> type = context->GetConcreteType(in_type);
  if (type->is_declaration())
    return Err("Can't resolve forward declaration for '%s'.", in_type->GetFullName().c_str());

  if (DwarfTagIsEitherReference(type->tag()))
    type = RefPtrTo(type->As<ModifiedType>()->modified().Get()->As<Type>());
  if (!type)
    return Err("Symbol error for '%s'.", in_type->GetFullName().c_str());

  return ExprValue(type->byte_size());
}

void TypeExprNode::EmitBytecode(VmStream& stream) const {
  // This is invalid. EmitBytecode can't report errors so generate some code to set the error at
  // runtime.
  stream.push_back(VmOp::MakeError(
      Err("Attempting to execute a type '" + type_->GetFullName() + "' as an expression.")));
}

void TypeExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "TYPE(";
  if (type_)
    out << type_->GetFullName();
  out << ")\n";
}

void UnaryOpExprNode::EmitBytecode(VmStream& stream) const {
  expr_->EmitBytecodeExpandRef(stream);
  stream.push_back(VmOp::MakeUnary(op_));
}

void UnaryOpExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "UNARY(" << op_.value() << ")\n";
  expr_->Print(out, indent + 1);
}

void VariableDeclExprNode::EmitBytecode(VmStream& stream) const {
  EmitVariableInitializerOps(decl_info_, local_slot_, init_expr_, stream);
}

void VariableDeclExprNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "LOCAL_VAR_DECL(" << name_.value() << ", " << local_slot_ << ")\n";
  out << IndentFor(indent + 1) << decl_info_.ToString() << "\n";
  PrintExprOrSemicolon(out, indent + 1, init_expr_);
}

}  // namespace zxdb
