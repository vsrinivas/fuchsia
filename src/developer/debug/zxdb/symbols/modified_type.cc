// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/modified_type.h"

#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/function_type.h"

namespace zxdb {

namespace {

// Returns true if this tag is a modified type that is transparent with respect to the data stored
// in it.
bool IsTransparentTag(DwarfTag tag) {
  return DwarfTagIsCVQualifier(tag) || tag == DwarfTag::kTypedef ||
         tag == DwarfTag::kImportedDeclaration;
}

}  // namespace

ModifiedType::ModifiedType(DwarfTag kind, LazySymbol modified) : Type(kind), modified_(modified) {
  FX_DCHECK(DwarfTagIsTypeModifier(kind));
  if (IsTransparentTag(kind)) {
    const Type* mod_type = modified_.Get()->AsType();
    if (mod_type)
      set_byte_size(mod_type->byte_size());
  } else if (DwarfTagIsPointerOrReference(kind)) {
    set_byte_size(kTargetPointerSize);
  }
}

ModifiedType::~ModifiedType() = default;

const ModifiedType* ModifiedType::AsModifiedType() const { return this; }

const Type* ModifiedType::StripCV() const {
  if (DwarfTagIsCVQualifier(tag())) {
    if (const Type* mod = modified_.Get()->AsType())  // Apply recursively.
      return mod->StripCV();
  }
  return this;
}

const Type* ModifiedType::StripCVT() const {
  if (IsTransparentTag(tag())) {
    const Type* mod = modified_.Get()->AsType();
    if (mod)  // Apply recursively.
      return mod->StripCVT();
  }
  return this;
}

bool ModifiedType::ModifiesVoid() const {
  // Void can be represented two ways, via a null modified type, or via a base type that's a "none"
  // type.
  if (!modified_)
    return true;

  const Type* type = modified_.Get()->AsType();
  if (!type) {
    // Corrupted symbols as this references a non-type or there was an error decoding. Say it's
    // non-void for the caller to handle when it tries to figure out what the type is.
    return false;
  }

  if (const BaseType* base = type->StripCVT()->AsBaseType())
    return base->base_type() == BaseType::kBaseTypeNone;
  return false;
}

std::string ModifiedType::ComputeFullName() const {
  static const char kUnknown[] = "<unknown>";

  // Typedefs are special and just use the assigned name. Every other modifier below is based on the
  // underlying type name.
  if (tag() == DwarfTag::kTypedef)
    return GetIdentifier().GetFullNameNoQual();

  const Type* modified_type = nullptr;
  std::string modified_name;
  if (!modified()) {
    // No modified type means "void".
    modified_name = "void";
  } else {
    if (auto func_type = modified().Get()->AsFunctionType();
        func_type && tag() == DwarfTag::kPointerType) {
      // Special-case pointer-to-funcion which has unusual syntax.
      // TODO(fxbug.dev/5533) this doesn't handle pointers of references to pointers-to-member functions
      return func_type->ComputeFullNameForFunctionPtr(std::string());
    } else if ((modified_type = modified().Get()->AsType())) {
      // All other types.
      modified_name = modified_type->GetFullName();
    } else {
      // Symbols likely corrupt.
      modified_name = kUnknown;
    }
  }

  switch (tag()) {
    case DwarfTag::kConstType:
      if (modified_type && modified_type->AsModifiedType()) {
        // When the underlying type is another modifier, add it to the end, e.g. a "constant pointer
        // to a nonconstant int" is "int* const".
        return modified_name + " const";
      } else {
        // Though the above formatting is always valid, most people write a "constant int" /
        // "pointer to a constant int" as either "const int" / "const int*" so special-case.
        return "const " + modified_name;
      }
    case DwarfTag::kPointerType:
      return modified_name + "*";
    case DwarfTag::kReferenceType:
      return modified_name + "&";
    case DwarfTag::kRestrictType:
      return modified_name + " restrict";
    case DwarfTag::kRvalueReferenceType:
      return modified_name + "&&";
    case DwarfTag::kVolatileType:
      return "volatile " + modified_name;
    case DwarfTag::kImportedDeclaration:
      // Using statements. This is use the kind that moves stuff between namespaces like "using
      // std::vector;" -- the renaming type is encoded as a typedef.
      //
      // TODO(brettw) this is probably wrong because we need to strip namespaces from the modified
      // type and instead use the namespaces from the using statement. Currently we don't encounter
      // these as Clang follows the using statement when defining types of variables so we only see
      // the underlying type. When we support type lookup by name, these will matter.
      return modified_name;
    default:
      return kUnknown;
  }
}

Identifier ModifiedType::ComputeIdentifier() const {
  // Typedefs are special and just use the assigned name.
  if (tag() == DwarfTag::kTypedef)
    return Symbol::ComputeIdentifier();

  // Every other modifier has decorations around it that means it can't have an identifier.
  return Identifier();
}

}  // namespace zxdb
