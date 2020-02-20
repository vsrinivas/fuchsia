// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VIRTUAL_INHERITANCE_TEST_SETUP_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VIRTUAL_INHERITANCE_TEST_SETUP_H_

#include <vector>

#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"

namespace zxdb {

class MockEvalContext;
class MockSymbolDataProvider;

// Setup the required information for a test hierarchy including virtual inheritance. Virtual
// inheritance in C++ isn't just inheritance with virtual functions, but rather:
//
//   class Derived : public virtual Bar { ... }
//
// The "virtual" in this case means that the base class is accessed indirectly, allowing diamond
// inheritance to be resolved. This indirect operation is expressed as an expression in the
// DWARF inheritance information rather than the constant offset used by normal inheritance.
//
// This class sets up an inheritance hierarchy with three steps of inheritance between four classes:
//
//   class Derived : public IntermediateDerived {                   // Non-virtual.
//     int derived_i = 1;
//   };
//   class IntermediateDerived : public virtual IntermediateBase {  // Virtual
//     int intermediate_derived_i = 2;
//   };
//   class IntermediateBase : public Base {                         // Non-virtual
//     int intermediate_base_i = 3;
//   };
//   class Base {
//     int base_i = 4;
//   };
//
// The binary structure looks like this:
//                                                               Value
//                          +----------------------------------+------------+
//               Derived -> | derived_i (4 bytes)              | 1          |
//                          +----------------------------------+------------+
//   IntermediateDerived -> | <vtable_ptr> (8 bytes)           | kVtablePtr |
//                          | intermediate_derived_i (4 bytes) | 2          |
//                          +----------------------------------+------------+
//      IntermediateBase -> | intermediate_base_i (4 bytes)    | 3          |
//                          +----------------------------------+------------+
//                  Base -> | base_i (4 bytes)                 | 4          |
//                          +----------------------------------+------------+
//
// Note that this is actually backwards than most compilers will generated (they will normally
// put "Base" at the beginning of "IntermediateBase") but doing it this way allows us to have an
// offset for each step of inheritance which is better for testing.
//
//                          +---------------------------------------------------------------------+
//  kVirtualDataAddress ->  | <offset of "IntermediateBase" from "IntermediateDerived"> (8 bytes) |
//                          | <some other value> (8 bytes)                                        |
//                          | <some other value> (8 bytes)                                        |
//                          +---------------------------------------------------------------------+
//            vtable_ptr -> | <vtable entries>                                                    |
//                          | ...                                                                 |
//
// The vtable_ptr referenced in the structure is 24 bytes after the offset needed (this is taken
// from what GCC generated for a test). This offset is retrieved and added to the
// IntermediateDerived pointer to get the address of IntermediateBase (so the offset should be 4).
//
// This uses GCC's style of expressions. See also ResolveCollectionTest.VirtualInheritance which
// tests Clang's version of virtual inheritance.
struct VirtualInheritanceTestSetup {
  VirtualInheritanceTestSetup();

  // Sets the mock vtable data to be served.
  void SaveMockData(MockSymbolDataProvider* mock) const;

  fxl::RefPtr<Collection> derived;
  fxl::RefPtr<InheritedFrom> intermediate_derived_inherited;  // Base -> IntermediateDerived.
  fxl::RefPtr<Collection> intermediate_derived;
  fxl::RefPtr<InheritedFrom> intermediate_base_inherited;  // IntermediateDerived->IntermediateBase
  fxl::RefPtr<Collection> intermediate_base;
  fxl::RefPtr<InheritedFrom> base_inherited;  // IntermediateBase -> Base
  fxl::RefPtr<Collection> base;

  // If the object is placed at derived_address, the other addresses here should follow.
  static constexpr TargetPointer kDerivedAddress = 0x12345678;
  static constexpr TargetPointer kIntermediateDerivedAddress = kDerivedAddress + 4;
  static constexpr TargetPointer kIntermediateBaseAddress = kIntermediateDerivedAddress + 12;
  static constexpr TargetPointer kBaseAddress = kIntermediateBaseAddress + 4;

  static constexpr TargetPointer kVirtualDataAddress = 0x01020304;
  static constexpr TargetPointer kVtablePtr = kVirtualDataAddress + 12;

  // Data representing a derived object.
  ExprValue derived_value;

  // Data that goes at kVirtualDataAddress.
  std::vector<uint8_t> virtual_data;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VIRTUAL_INHERITANCE_TEST_SETUP_H_
