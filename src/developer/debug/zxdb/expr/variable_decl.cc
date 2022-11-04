// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/variable_decl.h"

#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/vm_op.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// C++ rules for type deduction and conversions are "complicated." Our goal is to allow simple
// helper code in a natural way that looks like C++ (or Rust) without implementing very much of this
// logic. But we also don't want to subtly diverge from C++ in surprising ways. So we want a clear
// and simple subset of C++ behavior and give clear error messages for anything else.
//
// C++ "auto"
// ----------
//
// The "auto" type is important because the code snippets will often be decoding template data and
// the types won't be known in advance. The following is supported for "auto" (the debugger ignores
// "const"):
//
//  - auto  : Removes the reference if the right-hand-side expression is a reference.
//  - auto& : Keeps the reference if it exits (unlike bare "auto") and makes one if it doesn't.
//  - auto* : Like "auto" but verifies that the right-hand-side is a pointer.
//
// No other uses of "auto" in C++ is permitted for local variables. This means we can easily just
// enumerate the cases rather than write a complicated type matcher. Most users never use "auto"
// beyond this, and if the user does something unsupported, we can give a clear error message.
//
// C++ references
// --------------
//
// C++ reference initialization has special rules. When we see something like "Foo f = expr;" we
// would like to default-initialize "f" (in the debugger there are no side-effects so this is OK),
// cast the right-hand-side expression to a "Foo" using the normal casting logic, and then do the
// assignment. But this doesn't work if the left-hand type is a reference because initializers for
// references are different than for other types of assignment (it will implicitly take a pointer
// to the value in the initializer expression).
//
// To avoid this problem, we say you can't have references in the types of local variables that
// are anything other than "auto&". This keeps all of the reference logic in that one place and
// means we never have to convert types when making references. This can be annoying and we can
// enhance in the future, but at least we can give clear actionable error messages.
//
// Rust
// ----
//
// Rust's references are a bit easier. The type of a "let" expression with no explicit type is
// the exact type of the initializer, even if that initializer is a reference.

namespace {

bool IsCAutoType(const Type* type) { return type && type->GetAssignedName() == "auto"; }

// Walks the modified type hierarchy and returns true if any component of it is an "auto".
bool HasAnyCAutoType(const Type* type) {
  while (type) {
    if (IsCAutoType(type))
      return true;
    if (const ModifiedType* modified = type->As<ModifiedType>()) {
      type = modified->modified().Get()->As<Type>();
    } else {
      break;  // Not a modified type, done.
    }
  }
  return false;
}

// Ensures (if possible) that the given value is a reference. If it's not a reference, attempts to
// take a reference to the value (this is just it's address).
ErrOrValue ConvertToReference(const fxl::RefPtr<EvalContext> eval_context, const ExprValue& value) {
  fxl::RefPtr<Type> concrete_type = eval_context->GetConcreteType(value.type());
  if (!concrete_type)
    return Err("Variable initialization expression produced no results.");

  if (concrete_type->tag() == DwarfTag::kReferenceType)
    return value;  // Already a reference, nothing to do.

  // Take the address of the value if possible.
  if (value.source().type() != ExprValueSource::Type::kMemory) {
    return Err(
        "The initialization expression has no address (it's a temporary or optimized out)\n"
        "to get a reference to.");
  }
  TargetPointer value_addr = value.source().address();

  // Make the value containing the pointer data.
  auto ref_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, concrete_type);
  std::vector<uint8_t> ptr_data(sizeof(TargetPointer));
  memcpy(ptr_data.data(), &value_addr, sizeof(TargetPointer));
  return ExprValue(std::move(ref_type), std::move(ptr_data));
}

// Validates that the result value is a pointer type. Used for initialization to "auto*".
ErrOrValue EnsurePointer(const fxl::RefPtr<EvalContext> eval_context, const ExprValue& value) {
  fxl::RefPtr<Type> concrete_type = eval_context->GetConcreteType(value.type());
  if (!concrete_type)
    return Err("Variable initialization expression produced no results.");

  if (concrete_type->tag() != DwarfTag::kPointerType) {
    return Err("Can't match non-pointer initization expression of type '" +
               value.type()->GetFullName() + "' to 'auto*'.");
  }
  return value;  // Return the same value on success.
}

}  // namespace

std::string VariableDeclTypeInfo::ToString() const {
  switch (kind) {
    case kCAuto:
      return "<C++-style auto>";
    case kCAutoRef:
      return "<C++-style auto&>";
    case kCAutoPtr:
      return "<C++-style auto*>";
    case kRustAuto:
      return "<Rust-style auto>";
    case kExplicit:
      return concrete_type->GetFullName();
  }
  return "<Error>";
}

// Decodes any auto type specifiers for the variable declaration of the given type.
ErrOr<VariableDeclTypeInfo> GetVariableDeclTypeInfo(ExprLanguage lang,
                                                    fxl::RefPtr<Type> concrete_type) {
  if (!concrete_type) {
    switch (lang) {
      case ExprLanguage::kRust:
        return VariableDeclTypeInfo{.kind = VariableDeclTypeInfo::kRustAuto};
      case ExprLanguage::kC:
        return VariableDeclTypeInfo{.kind = VariableDeclTypeInfo::kCAuto};
    }
  }

  if (lang == ExprLanguage::kRust)
    return VariableDeclTypeInfo{.kind = VariableDeclTypeInfo::kExplicit,
                                .concrete_type = std::move(concrete_type)};

  // Everything below here is for C which always requires a type name (even if it's "auto").
  if (IsCAutoType(concrete_type.get()))
    return VariableDeclTypeInfo{.kind = VariableDeclTypeInfo::kCAuto};

  // On the concrete type, things like "const" will have been stripped so we can check for pointers
  // and references directly.
  if (auto modified = concrete_type->As<ModifiedType>()) {
    if (modified->tag() == DwarfTag::kPointerType) {
      if (IsCAutoType(modified->modified().Get()->As<Type>()))
        return VariableDeclTypeInfo{.kind = VariableDeclTypeInfo::kCAutoPtr};
    }
    if (modified->tag() == DwarfTag::kReferenceType) {
      if (IsCAutoType(modified->modified().Get()->As<Type>()))
        return VariableDeclTypeInfo{.kind = VariableDeclTypeInfo::kCAutoRef};
    }
  }

  if (HasAnyCAutoType(concrete_type.get()))
    return Err("Only 'auto', 'auto*' and 'auto&' variable types are supported in the debugger.");

  return VariableDeclTypeInfo{.kind = VariableDeclTypeInfo::kExplicit,
                              .concrete_type = std::move(concrete_type)};
}

void EmitVariableInitializerOps(const VariableDeclTypeInfo& decl_info, uint32_t local_slot,
                                fxl::RefPtr<ExprNode> init_expr, VmStream& stream) {
  // Evaluate the init expression.
  if (init_expr) {
    // Have an init expression, evaluate it.
    if (decl_info.kind == VariableDeclTypeInfo::kCAuto) {
      // In C++, "auto" expands the value of a reference, not the reference type itself.
      init_expr->EmitBytecodeExpandRef(stream);
    } else {
      init_expr->EmitBytecode(stream);
    }
  } else {
    // No init expression, we must have a concrete type.
    FX_DCHECK(decl_info.kind == VariableDeclTypeInfo::kExplicit);

    // Default-initialize a variable of the requested type. Our default initialization is 0's.
    size_t byte_size = decl_info.concrete_type->byte_size();
    stream.push_back(
        VmOp::MakeLiteral(ExprValue(decl_info.concrete_type, std::vector<uint8_t>(byte_size, 0))));
  }

  // Convert / valudate the result type.
  switch (decl_info.kind) {
    case VariableDeclTypeInfo::kCAuto:
    case VariableDeclTypeInfo::kRustAuto:
      // These get stored directly (any references will have already been stripped for C++).
      // Break out to continue storing.
      break;

    case VariableDeclTypeInfo::kCAutoRef:
      stream.push_back(VmOp::MakeCallback1(&ConvertToReference));
      break;

    case VariableDeclTypeInfo::kCAutoPtr:
      stream.push_back(VmOp::MakeCallback1(&EnsurePointer));
      break;

    case VariableDeclTypeInfo::kExplicit:
      // Cast the result of the expression to the desired result type.
      stream.push_back(
          VmOp::MakeAsyncCallback1([decl_info](const fxl::RefPtr<EvalContext>& eval_context,
                                               ExprValue value, EvalCallback cb) {
            CastExprValue(eval_context, CastType::kImplicit, value, decl_info.concrete_type,
                          ExprValueSource(), std::move(cb));
          }));
      break;
  }

  // The variable value is now on the stack. We need one copy to save as a local, the other
  // copy to leave on the stack as the "result" of this expression.
  stream.push_back(VmOp::MakeDup());
  stream.push_back(VmOp::MakeSetLocal(local_slot));
}

}  // namespace zxdb
