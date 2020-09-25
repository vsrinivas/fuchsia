// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/collection.h"

#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

Collection::Collection(DwarfTag tag, std::string name) : Type(tag) {
  set_assigned_name(std::move(name));
}

Collection::~Collection() = default;

const Collection* Collection::AsCollection() const { return this; }

Collection::SpecialType Collection::GetSpecialType() const {
  if (!special_type_)
    special_type_ = ComputeSpecialType();
  return *special_type_;
}

const char* Collection::GetKindString() const {
  switch (tag()) {
    case DwarfTag::kStructureType:
      return "struct";
    case DwarfTag::kClassType:
      return "class";
    case DwarfTag::kUnionType:
      return "union";
    default:
      return "unknown";
  }
}

std::string Collection::ComputeFullName() const {
  // Some compiler-generated classes have no names. Clang does this for the
  // implicit classes that hold closure values. So provide a better description
  // when those are printed. This isn't qualified with namespaces because that
  // doesn't add much value when there's no name.
  const std::string& assigned_name = GetAssignedName();
  if (assigned_name.empty())
    return fxl::StringPrintf("(anon %s)", GetKindString());
  return Symbol::ComputeFullName();
}

Collection::SpecialType Collection::ComputeSpecialType() const {
  // All the special types use "structure" as their tag.
  if (tag() != DwarfTag::kStructureType)
    return kNotSpecial;

  // As of this writing the language is slower to check because it involves
  // decoding the CompileUnit which is not normally otherwise needed. As a
  // result we try to check that last.

  if (variant_part_) {
    // Having a variant part and no members currently means we consider it a
    // Rust enum. This may need to be enhanced for fxbug.dev/6466 (support Rust
    // generators).
    //
    // No other special types have variants.
    if (data_members_.empty() && GetLanguage() == DwarfLang::kRust)
      return kRustEnum;
    return kNotSpecial;
  }

  // Everything below requires at least one member.
  if (data_members_.empty())
    return kNotSpecial;

  // Here we're checking for the two types of Rust tuples, regular tuples and
  // tuple structs. Prefer to decode the first data member (it's very likely to
  // be needed later no matter what) before the language.
  //
  // Tuples have members labeled starting counting from 0 with two leading
  // underscores: __0, __1, __2, etc. With this heuristic we assume they're
  // encoded in order in the symbols and that __0 will be first. Currently
  // we check only the first one but this could be more precise (if slower)
  // by checking to make sure all members are such numbers in sequential order.
  const DataMember* first_member = data_members_[0].Get()->AsDataMember();
  if (!first_member)
    return kNotSpecial;

  if (first_member->GetAssignedName() != "__0")
    return kNotSpecial;

  // Now that we're pretty sure this looks like some kind of Rust tuple, double
  // check the language.
  if (GetLanguage() != DwarfLang::kRust)
    return kNotSpecial;

  // Either type of Rust tuple. Regular types are assigned names like
  // "(i32, i32)" while struct tuples have regular names. Use the assigned name
  // and not the full name because we don't want any namespace qualifications.
  const std::string& assigned_name = GetAssignedName();
  if (assigned_name.size() > 2 && assigned_name[0] == '(' && assigned_name.back() == ')')
    return kRustTuple;
  return kRustTupleStruct;
}

}  // namespace zxdb
