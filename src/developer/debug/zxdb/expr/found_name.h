// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/expr/found_member.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

// This class represents the result of looking up a variable by name. It could
// be a local or global variable (simple Variable* object), or it could be a
// member of the current implicit object ("this" in C++). This class represents
// either state.
class FoundName {
 public:
  // Since identifiers with template parameters at the end are assumed to be
  // a type, we don't need to check that "std::vector<int>" is a type. This
  // will need to be revisited if we support templatized function names in
  // expressions ("auto a = &MyClass::MyFunc<int>;");
  enum Kind {
    kNone,            // Nothing with this name found.
    kVariable,        // Local and global variables.
    kMemberVariable,  // Class and struct member vars that require an object.
    kNamespace,       // Namespace name like "std".
    kTemplate,        // Template name without parameters like "std::vector".
    kType,            // Full type name like "std::string" or "int".
    kFunction,
  };

  // Default constructor for a "not found" name.
  FoundName();

  // Constructor for templates and namespaces that have no extra data.
  explicit FoundName(Kind kind);

  // Takes a reference to the object.
  explicit FoundName(const Variable* variable);
  explicit FoundName(const Function* function);

  // Constructor for data member variables. The object_ptr may be null if this
  // represents a query on a type with no corresponding variable).
  FoundName(const Variable* object_ptr, FoundMember member);
  FoundName(const Variable* object_ptr, const DataMember* data_member,
            uint32_t data_member_offset);

  // Constructor for types.
  explicit FoundName(fxl::RefPtr<Type> type);

  ~FoundName();

  Kind kind() const { return kind_; }

  // Implicit conversion to bool to test for a found value.
  bool is_found() const { return kind_ != kNone; }
  operator bool() const { return kind_ != kNone; }

  // Use when kind == kVariable and kMemberVariable. The variable may be null
  // for member pointers if the call is just looking up the
  const Variable* variable() const { return variable_.get(); }
  fxl::RefPtr<Variable> variable_ref() { return variable_; }

  // Valid when kind == kMemberVariable. Always use the data_member_offset()
  // from this class rather than the offset in data_member() (see comment
  // below for more).
  const Variable* object_ptr() const { return object_ptr_.get(); }
  const FoundMember& member() const { return member_; }

  // Valid when kind == kType.
  fxl::RefPtr<Type>& type() { return type_; }
  const fxl::RefPtr<Type>& type() const { return type_; }

  // Valid when kind == kFunction.
  fxl::RefPtr<Function>& function() { return function_; }
  const fxl::RefPtr<Function>& function() const { return function_; }

 private:
  Kind kind_ = kNone;

  // Represents the found variable when it's not a class member. When null,
  // the result will be in object_member/data_member.
  fxl::RefPtr<Variable> variable_;

  // Represents the "this" object the data member is associated with it.
  // Non-null when the found variable is a collection member. In this case,
  // data_member and data_member_offset will be valid.
  //
  // This is the outermost object which one would evaluate to get the value of
  // the object pointer rather than the class the data member is declared in
  // (it could be a base class).
  fxl::RefPtr<Variable> object_ptr_;

  // Valid when object_ptr_ is non-null. This indicates the location of the
  // data inside the object.
  FoundMember member_;

  fxl::RefPtr<Type> type_;
  fxl::RefPtr<Function> function_;
};

}  // namespace zxdb
