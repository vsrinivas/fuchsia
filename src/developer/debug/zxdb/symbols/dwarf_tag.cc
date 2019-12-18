// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"

namespace zxdb {

bool DwarfTagIsType(DwarfTag tag) {
  return DwarfTagIsTypeModifier(tag) || tag == DwarfTag::kArrayType || tag == DwarfTag::kBaseType ||
         tag == DwarfTag::kClassType || tag == DwarfTag::kEnumerationType ||
         tag == DwarfTag::kPtrToMemberType || tag == DwarfTag::kStringType ||
         tag == DwarfTag::kStructureType || tag == DwarfTag::kSubroutineType ||
         tag == DwarfTag::kUnionType || tag == DwarfTag::kUnspecifiedType;

  // Types in languages we ignore for now:
  //   kInterfaceType
  //   kSetType
  //   kSharedType
  //   kPackedType
  //   kFileType
}

bool DwarfTagIsTypeModifier(DwarfTag tag) {
  return tag == DwarfTag::kConstType || tag == DwarfTag::kPointerType ||
         tag == DwarfTag::kReferenceType || tag == DwarfTag::kRestrictType ||
         tag == DwarfTag::kRvalueReferenceType || tag == DwarfTag::kTypedef ||
         tag == DwarfTag::kVolatileType || tag == DwarfTag::kImportedDeclaration;
}

bool DwarfTagIsCVQualifier(DwarfTag tag) {
  return tag == DwarfTag::kConstType || tag == DwarfTag::kVolatileType ||
         tag == DwarfTag::kRestrictType;
}

bool DwarfTagIsEitherReference(DwarfTag tag) {
  return tag == DwarfTag::kReferenceType || tag == DwarfTag::kRvalueReferenceType;
}

bool DwarfTagIsPointerOrReference(DwarfTag tag) {
  return DwarfTagIsEitherReference(tag) || tag == DwarfTag::kPointerType;
}

}  // namespace zxdb
