// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is compiled into a library and used in zxdb tests to query
// symbol information. The actual code is not run.

#include "src/developer/debug/zxdb/symbols/test_data/zxdb_symbol_test.h"

// DW_TAG_namespace
//   DW_AT_name = "my_ns"
namespace my_ns {

// DW_TAG_variable
//   DW_AT_name = "kGlobal
//   DW_AT_type = <ref to DIE declaring "int">
//   DW_AT_external = true
//   DW_AT_decl_file = ...
//   DW_AT_decl_line = ...
//   DW_AT_location = ...
//   DW_AT_linkage_name = "_ZN5my_ns7kGlobalE"
//
//   (Unlike MyClass::kClassStatic, this variable shared the declaration and
//   storage.)
int kGlobal = 19;

// DW_TAG_class_type
//   DW_AT_name = "MyClass"
class MyClass {
 public:
  // DW_TAG_structure_type
  //   DW_AT_name = "Inner"
  struct Inner {
    // DW_TAG_subprogram
    //   DW_AT_name = "MyMemberTwo"
    //   DW_AT_linkage_name = "_ZN5my_ns7MyClass11MyMemberOneEv"
    //   DW_AT_declaration = true (indicates implementation is elsewhere).
    //   DW_AT_type = <ref to DIE declaring "int">
    NOINLINE static int MyMemberTwo();
  };

  // DW_TAG_member
  //   DW_AT_name = "kClassStatic"
  //   DW_AT_type = <ref to DIE declaring "int">
  //   DW_AT_decl_file = ...
  //   DW_AT_decl_line = ...
  //   DW_AT_external = true
  //   DW_AT_declaration = true
  static int kClassStatic;

  // DW_TAG_subprogram
  //   DW_AT_name = "MyMemberOne"
  //   DW_AT_linkage_name = "_ZN5my_ns7MyClass11MyMemberOneEv"
  //   DW_AT_declaration = true (indicates implementation is elsewhere).
  //   DW_AT_type = <ref to DIE declaring "int">
  //
  //   DW_TAG_formal_parameter
  //     DW_AT_artificial = true ("this" is the implicit parameter).
  //     DW_AT_type = <reference to "MyClass*" type>
  NOINLINE int MyMemberOne() { return 42; }
};

// DW_TAG_variable
//   DW_AT_specification = <ref to DIE declaring this class member>.
//   DW_AT_location = ...
//   DW_AT_linkage_name "_ZN5my_ns7MyClass12kClassStaticE"
//
//   (Unlike kGlobal, this static has a separate declaration and storage.)
int MyClass::kClassStatic = 12;

// The DW_TAG_namespace will end here even though the namespace encloses the
// following function definition. The namespace is only used for the
// definitions, while the actual implementations have no namespacing or class
// information (you have to follow DW_AT_specification to find the declaration
// to get this stuff).

// DW_TAG_subprogram
//   DW_AT_low_pc = ...  (these indicate there's actually code)
//   DW_AT_high_pc = ...
//   DW_AT_specification = <ref to the DIE for the definition>
NOINLINE int MyClass::Inner::MyMemberTwo() { return 61; }

// A function inside the namespace. This function has no separate definition.
EXPORT int NamespaceFunction() { return 78; }

}  // namespace my_ns

// DW_TAG_namespace
//   (no name)
namespace {

// DW_TAG_subprogram
//    (The compiler *really* likes to strip anonymous namespace functions, even
//    when marked "noinline". The parameter being passed in from a parameter
//    from an exported function is required to prevent this).
NOINLINE int AnonNSFunction(int i) {
  return i + 5;
}

}  // namespace

// DW_TAG_subprogram
//   DW_AT_low_pc = ... (indicates there's code).
//   DW_AT_high_pc = ...
//   DW_AT_linkage_name = "_Z10MyFunctionv"
//   DW_AT_type = <ref to DIE declaring "int">
//
//   (This one has no declaration nor specification attributes because there
//   wasn't a separate declaration.)
EXPORT int MyFunction(int i) {  // Must be on line # ModuleSymbols::kMyFunctionLine.
  // DW_TAG_variable
  //   DW_AT_name = "my_class"
  //   DW_AT_type = <reference to MyClass DIE above>
  my_ns::MyClass my_class;
  return my_class.MyMemberOne() + my_ns::NamespaceFunction() +
         my_ns::MyClass::Inner::MyMemberTwo() + AnonNSFunction(i);
}

// The inplementation of the MyClass::MyMemberOne will be inserted somewhere in
// the unit and it will reference the declaration.
//
// DW_TAG_subprogram
//   DW_AT_low_pc = ... (indicates there's code).
//   DW_AT_high_pc = ...
//   DW_AT_specification = <ref to the DIE for "MyClass::MyMemberOne">

// Somewhere in the unit these types will be defined which are referenced as
// types from the various functions. There could also be dupes!
//
// DW_TAG_base_type
//    DW_AT_name = "int"
//    DW_AT_encoding = DW_ATE_signed
//    DW_AT_byte_size = 4
//
// DW_TAG_pointer_type  ("MyClass*" which is used as the "this" param type.)
//    DW_AT_type = <reference to "MyClass" DIE above>
