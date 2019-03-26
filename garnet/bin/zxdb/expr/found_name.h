// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/expr/found_member.h"
#include "garnet/bin/zxdb/symbols/variable.h"

namespace zxdb {

// This class represents the result of looking up a variable by name. It could
// be a local or global variable (simple Variable* object), or it could be a
// member of the current implicit object ("this" in C++). This class represents
// either state.
class FoundName {
 public:
  // Constructor for regular variables. Takes a reference to the object.
  explicit FoundName(const Variable* variable);

  // Constructor for data members.
  FoundName(const Variable* object_ptr, FoundMember member);
  FoundName(const Variable* object_ptr, const DataMember* data_member,
            uint32_t data_member_offset);

  ~FoundName();

  bool is_object_member() const { return !!object_ptr_; }

  // Use when is_object_member() is false.
  const Variable* variable() const { return variable_.get(); }
  fxl::RefPtr<Variable> variable_ref() { return variable_; }

  // Use when is_object_member() is true. Always use the data_member_offset()
  // from this class rather than the offset in data_member() (see comment
  // below for more).
  const Variable* object_ptr() const { return object_ptr_.get(); }
  const FoundMember& member() const { return member_; }

 private:
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
};

}  // namespace zxdb
