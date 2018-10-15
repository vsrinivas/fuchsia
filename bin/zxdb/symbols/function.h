// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/symbols/code_block.h"
#include "garnet/bin/zxdb/symbols/file_line.h"

namespace zxdb {

// Represents a function (a "subprogram" in DWARF parlance).
//
// Some functions in DWARF are "implementations" that have code ranges
// associated with them, and some are "specifications" (akin to C forward
// declarations) that don't. The context about the namespaces and parent
// classes comes from the specification, while the implementation of the
// function may be outside of any namespace or class definitions.
//
// It seems Clang puts the function parameters in both places, some attributes
// like DW_AT_frame_base will only be on the implementation, and others like
// DW_AT_decl_file/line, DW_AT_accessibility, and the return type (DW_AT_type)
// are only on the specification.
//
// In the case of an implementation, the decoder will attempt to fill in the
// attributes from the specification automatically so this Function object
// will have full context. Be aware that this won't necessarily match the
// DIE that generated the object.
class Function final : public CodeBlock {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const Function* AsFunction() const override;
  const std::string& GetAssignedName() const final { return assigned_name_; }

  // TODO(brettw) this needs more stuff like DW_AT_frame_base.

  // Unmangled name. Does not include any class or namespace qualifications.
  // (see Symbol::GetAssignedName)
  void set_assigned_name(std::string n) { assigned_name_ = std::move(n); }

  // Mangled name.
  const std::string& linkage_name() const { return linkage_name_; }
  void set_linkage_name(std::string n) { linkage_name_ = std::move(n); }

  // The location in the source code of the declaration. May be empty.
  const FileLine& decl_line() const { return decl_line_; }
  void set_decl_line(FileLine decl) { decl_line_ = std::move(decl); }

  // The return value type.
  const LazySymbol& return_type() const { return return_type_; }
  void set_return_type(const LazySymbol& rt) { return_type_ = rt; }

  // Parameters passed to the function.
  const std::vector<LazySymbol>& parameters() const { return parameters_; }
  void set_parameters(std::vector<LazySymbol> p) { parameters_ = std::move(p); }

  // The object pointer will be a reference to a parameter (which should
  // theoretically match one of the entries in the parameters() list but we
  // can't guarantee what the compiler has generated) for the implicit object
  // ("this") pointer for member functions. For nonmember or static member
  // functions the object pointer will be null.
  const LazySymbol& object_pointer() const { return object_pointer_; }
  void set_object_pointer(const LazySymbol& op) { object_pointer_ = op; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Function);
  FRIEND_MAKE_REF_COUNTED(Function);

  Function();
  ~Function();

  // Symbol protected overrides.
  std::string ComputeFullName() const override;

  std::string assigned_name_;
  std::string linkage_name_;
  FileLine decl_line_;
  LazySymbol return_type_;
  std::vector<LazySymbol> parameters_;
  LazySymbol object_pointer_;
};

}  // namespace zxdb
