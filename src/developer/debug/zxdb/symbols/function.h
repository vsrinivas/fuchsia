// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_FUNCTION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_FUNCTION_H_

#include <string>

#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/file_line.h"
#include "src/developer/debug/zxdb/symbols/variable_location.h"

namespace zxdb {

// Represents a function (a "subprogram" in DWARF parlance). This is different than a "FunctionType"
// which is the type used to represent function pointers.
//
// Some functions in DWARF are "implementations" that have code ranges associated with them, and
// some are "specifications" (akin to C forward declarations) that don't. The context about the
// namespaces and parent classes comes from the specification, while the implementation of the
// function may be outside of any namespace or class definitions.
//
// It seems Clang puts the function parameters in both places, some attributes like DW_AT_frame_base
// will only be on the implementation, and others like DW_AT_decl_file/line, DW_AT_accessibility,
// and the return type (DW_AT_type) are only on the specification.
//
// In the case of an implementation, the decoder will attempt to fill in the attributes from the
// specification automatically so this Function object will have full context. Be aware that this
// won't necessarily match the DIE that generated the object.
//
// NAMING: The "full name" (as returned by Symbol::GetFullName()) of a function is the qualified
// name without any return types or parameters. Some callers may want parameters, we can add a
// helper function in the future if necessary (for display we would often want to syntax highlight
// these differently so this is often better done at a different layer).
class Function final : public CodeBlock {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const Function* AsFunction() const override;
  const std::string& GetAssignedName() const final { return assigned_name_; }

  // Returns true if this function is an inlined function instance.
  bool is_inline() const { return tag() == DwarfTag::kInlinedSubroutine; }

  // The containing block is the CodeBlock that contains an inlined function.  This will be null for
  // non-inlined functions.
  //
  // For inlined functions, Symbol::parent() will contain the lexical parent of the inlined function
  // (a class or namespace) while the containing block will be the CodeBlock (of any type) that the
  // code is inlined into.
  //
  // "Uncached" symbols must be used for all upward-pointing symbol references to prevent cycles.
  const UncachedLazySymbol& containing_block() const { return containing_block_; }
  void set_containing_block(UncachedLazySymbol c) { containing_block_ = std::move(c); }

  // Unmangled name. Does not include any class or namespace qualifications.  (see
  // Symbol::GetAssignedName)
  void set_assigned_name(std::string n) { assigned_name_ = std::move(n); }

  // Mangled name.
  const std::string& linkage_name() const { return linkage_name_; }
  void set_linkage_name(std::string n) { linkage_name_ = std::move(n); }

  // The location in the source code of the declaration. May be empty.
  const FileLine& decl_line() const { return decl_line_; }
  void set_decl_line(FileLine decl) { decl_line_ = std::move(decl); }

  // For inline functions, this can be set to indicate the call location.
  const FileLine& call_line() const { return call_line_; }
  void set_call_line(FileLine call) { call_line_ = std::move(call); }

  // The return value type. This should be some kind of Type object. Will be empty for void return
  // types.
  const LazySymbol& return_type() const { return return_type_; }
  void set_return_type(const LazySymbol& rt) { return_type_ = rt; }

  // Parameters passed to the function. These should be Variable objects.
  const std::vector<LazySymbol>& parameters() const { return parameters_; }
  void set_parameters(std::vector<LazySymbol> p) { parameters_ = std::move(p); }

  // Template parameters if this function is a template instantiation.
  const std::vector<LazySymbol>& template_params() const { return template_params_; }
  void set_template_params(std::vector<LazySymbol> p) { template_params_ = std::move(p); }

  // The frame base is the location where "fbreg" expressions are evaluated relative to (this will
  // be most local variables in a function). This can be an empty location if there's no frame base
  // or it's not needed (e.g.  for inline functions).
  //
  // When compiled with full stack frames, this will usually evaluate to the contents of the CPU's
  // "BP" register, but can be different or arbitrarily complicated, especially when things are
  // optimized.
  const VariableLocation& frame_base() const { return frame_base_; }
  void set_frame_base(VariableLocation base) { frame_base_ = std::move(base); }

  // The object pointer is the "this" object for the current function.  Quick summary: Use
  // GetObjectPointerVariable() to retrieve "this".
  //
  // The object_pointer() will be a reference to a parameter (object type Variable). It should
  // theoretically match one of the entries in the parameters() list but we can't guarantee what the
  // compiler has generated.  The variable will be the implicit object ("this") pointer for member
  // functions. For nonmember or static member functions the object pointer will be null.
  //
  // For inlined functions the location on the object_pointer variable may be wrong. Typically an
  // inlined subroutine consists of two entries:
  //
  //   Shared entry for all inlined instances:
  //   (1)  DW_TAG_subprogram "InlinedFunc"
  //             DW_AT_object_pointer = reference to (2)
  //   (2)    DW_TAG_formal_parameter "this"
  //               <info on the parameter>
  //
  //   Specific inlined function instance:
  //   (3)  DW_TAG_inlined_subroutine "InlinedFunc"
  //              DW_AT_abstract_origin = reference to (1)
  //   (4)    DW_TAG_formal_parameter "this"
  //                DW_AT_abstract_origin = reference to (2)
  //                <info on parameter, possibly different than (2)>
  //
  // Looking at the object pointer will give the variable on the abstract origin, while the inlined
  // subroutine will have its own declaration for "this" which will have a location specific to this
  // inlined instantiation.
  //
  // The GetObjectPointerVariable() function handles this case and returns the correct resulting
  // variable. It will return null if there is no object pointer.
  const LazySymbol& object_pointer() const { return object_pointer_; }
  void set_object_pointer(const LazySymbol& op) { object_pointer_ = op; }
  const Variable* GetObjectPointerVariable() const;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Function);
  FRIEND_MAKE_REF_COUNTED(Function);

  // The tag must be either "subprogram" or "inlined subroutine" according to whether or not this is
  // an inlined function.
  explicit Function(DwarfTag tag);
  ~Function();

  // Symbol protected overrides.
  Identifier ComputeIdentifier() const override;

  UncachedLazySymbol containing_block_;
  std::string assigned_name_;
  std::string linkage_name_;
  FileLine decl_line_;
  FileLine call_line_;
  LazySymbol return_type_;
  std::vector<LazySymbol> parameters_;
  std::vector<LazySymbol> template_params_;
  VariableLocation frame_base_;
  LazySymbol object_pointer_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_FUNCTION_H_
