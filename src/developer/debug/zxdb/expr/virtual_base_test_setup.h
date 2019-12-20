// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VIRTUAL_BASE_TEST_SETUP_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VIRTUAL_BASE_TEST_SETUP_H_

#include <vector>

#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

class MockEvalContext;

// Sets up the required information for a test involving a base class with a virtual function and
// a class derived from it. The necessary symbols for the vtables all need to be set properly.
//
// Example:
//
//    VirtualBaseTestSetup setup(&eval_context);
//    ExprValue ptr(setup::kBaseAddress, setup.base_class_ptr);
//
// This will be a pointer to the Base class that is actually implemented by the Derived class.
struct VirtualBaseTestSetup {
  explicit VirtualBaseTestSetup(MockEvalContext* eval_context);

  // Clang uses "vtbl_ptr_type*" as the type for the vtable pointers at the beginning of a virtual
  // class. Clang defines the vtable as being pointers to functions "int()", so a pointer to a table
  // is a pointer to that. For simplicity, we define the vtable entry type as uint64_t instead of
  // "int()".
  fxl::RefPtr<Type> vtbl_entry_type;
  fxl::RefPtr<Type> vtbl_ptr_type;
  fxl::RefPtr<Type> vtbl_ptr_type_ptr;

  // Virtual base class collection type. It has one member "base_i" = kBaseI defined below.
  fxl::RefPtr<Collection> base_class;

  fxl::RefPtr<ModifiedType> base_class_ptr;  // BaseClass*
  fxl::RefPtr<ModifiedType> base_class_ref;  // BaseClass&

  // This starts the base class at an offset inside of the derived one, leaving empty bytes at the
  // beginning. This simulates having multiple inheritance and tests the offset management.
  static constexpr uint32_t kBaseOffset = 4;

  // Derived class type. It has one member "derived_i" = kDerivedI defined below.
  fxl::RefPtr<Collection> derived_class;

  // Address of the derived class' vtable.
  static constexpr TargetPointer kVtableAddress = 0x200000;

  // Address of the derived and base class in memory.
  static constexpr TargetPointer kDerivedAddress = 0x1000;
  static constexpr TargetPointer kBaseAddress = kDerivedAddress + kBaseOffset;

  // Symbols for the vtables. Make kVtableAddress can point to one of these to determine the type of
  // the class.
  fxl::RefPtr<ElfSymbol> base_vtable;
  fxl::RefPtr<ElfSymbol> derived_vtable;

  // The values of the two data members in the derived_data.
  static constexpr uint32_t kBaseI = 42;
  static constexpr uint32_t kDerivedI = 99;

  // Sample data for the derived class. This will have a vtable address of kVtableAddress and
  // will be injected into the mock eval context at kDerivedAddress.
  std::vector<uint8_t> derived_data;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VIRTUAL_BASE_TEST_SETUP_H_
