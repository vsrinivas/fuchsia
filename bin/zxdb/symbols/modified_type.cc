// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/modified_type.h"

namespace zxdb {

namespace {

// Returns true if this tag is a modified type that is transparent with respect
// to the data stored in it.
bool IsTransparentTag(int tag) {
  return tag == Symbol::kTagConstType || tag == Symbol::kTagVolatileType ||
         tag == Symbol::kTagTypedef;
}

// Returns true if this modified holds some kind of pointer to the modified
// type.
bool IsPointerTag(int tag) {
  return tag == Symbol::kTagPointerType || tag == Symbol::kTagReferenceType ||
         tag == Symbol::kTagRvalueReferenceType;
}

}  // namespace

ModifiedType::ModifiedType(int kind, LazySymbol modified)
    : Type(kind), modified_(modified) {
  if (IsTransparentTag(kind)) {
    const Type* mod_type = modified_.Get()->AsType();
    if (mod_type)
      set_byte_size(mod_type->byte_size());
  } else if (IsPointerTag(kind)) {
    // Assume 64-bit pointers.
    set_byte_size(8);
  }
}

ModifiedType::~ModifiedType() = default;

const ModifiedType* ModifiedType::AsModifiedType() const { return this; }

const Type* ModifiedType::GetConcreteType() const {
  if (IsTransparentTag(tag())) {
    const Type* mod = modified_.Get()->AsType();
    if (mod)
      return mod->GetConcreteType();
  }
  return this;
}

// static
bool ModifiedType::IsTypeModifierTag(int tag) {
  return tag == kTagConstType || tag == kTagPointerType ||
         tag == kTagReferenceType || tag == kTagRestrictType ||
         tag == kTagRvalueReferenceType || tag == kTagTypedef ||
         tag == kTagVolatileType || tag == kTagImportedDeclaration;
}

std::string ModifiedType::ComputeFullName() const {
  static const char kUnknown[] = "<unknown>";

  // Typedefs are special and just use the assigned name. Every other modifier
  // below is based on the underlying type name.
  if (tag() == kTagTypedef)
    return GetAssignedName();

  const Type* modified_type = nullptr;
  std::string modified_name;
  if (!modified()) {
    // No modified type means "void".
    modified_name = "void";
  } else {
    if ((modified_type = modified().Get()->AsType()))
      modified_name = modified_type->GetFullName();
    else
      modified_name = kUnknown;  // Symbols likely corrupt.
  }

  switch (tag()) {
    case kTagConstType:
      if (modified_type && modified_type->AsModifiedType()) {
        // When the underlying type is another modifier, add it to the end,
        // e.g. a "constant pointer to a nonconstant int" is "int* const".
        return modified_name + " const";
      } else {
        // Though the above formatting is always valid, most people write a
        // "constant int" / "pointer to a constant int" as either "const int" /
        // "const int*" so special-case.
        return "const " + modified_name;
      }
    case kTagPointerType:
      return modified_name + "*";
    case kTagReferenceType:
      return modified_name + "&";
    case kTagRestrictType:
      return "restrict " + modified_name;
    case kTagRvalueReferenceType:
      return modified_name + "&&";
    case kTagVolatileType:
      return "volatile " + modified_name;
    case kTagImportedDeclaration:
      // Using statements use the underlying name.
      return modified_name;
  }
  return kUnknown;
}

}  // namespace zxdb
