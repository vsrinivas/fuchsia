// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/type.h"

#include "src/developer/debug/zxdb/symbols/symbol_utils.h"

namespace zxdb {

Symbol::Symbol() = default;
Symbol::Symbol(DwarfTag tag) : tag_(tag) {}
Symbol::~Symbol() = default;

const std::string& Symbol::GetAssignedName() const {
  const static std::string empty;
  return empty;
}

const std::string& Symbol::GetFullName() const {
  if (!computed_full_name_) {
    computed_full_name_ = true;
    full_name_ = ComputeFullName();
  }
  return full_name_;
}

const ArrayType* Symbol::AsArrayType() const { return nullptr; }
const BaseType* Symbol::AsBaseType() const { return nullptr; }
const CodeBlock* Symbol::AsCodeBlock() const { return nullptr; }
const Collection* Symbol::AsCollection() const { return nullptr; }
const DataMember* Symbol::AsDataMember() const { return nullptr; }
const Enumeration* Symbol::AsEnumeration() const { return nullptr; }
const Function* Symbol::AsFunction() const { return nullptr; }
const FunctionType* Symbol::AsFunctionType() const { return nullptr; }
const InheritedFrom* Symbol::AsInheritedFrom() const { return nullptr; }
const MemberPtr* Symbol::AsMemberPtr() const { return nullptr; }
const ModifiedType* Symbol::AsModifiedType() const { return nullptr; }
const Namespace* Symbol::AsNamespace() const { return nullptr; }
const Type* Symbol::AsType() const { return nullptr; }
const Value* Symbol::AsValue() const { return nullptr; }
const Variable* Symbol::AsVariable() const { return nullptr; }

std::string Symbol::ComputeFullName() const {
  const std::string& assigned_name = GetAssignedName();
  if (assigned_name.empty()) {
    // When a thing doesn't have a name, don't try to qualify it, since
    // returning "foo::" for the name of something like a lexical block is
    // actively confusing.
    return std::string();
  }

  // This base type class just uses the qualified name for the full name.
  // Derived classes will override this function to apply modifiers.
  return GetSymbolScopePrefix(this) + assigned_name;
}

}  // namespace zxdb
