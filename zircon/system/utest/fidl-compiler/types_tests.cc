// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/types.h>
#include <zxtest/zxtest.h>

#include "test_library.h"

namespace fidl {
namespace flat {

void CheckPrimitiveType(const Library* library, Typespace* typespace, const char* name,
                        types::PrimitiveSubtype subtype) {
  ASSERT_NOT_NULL(typespace);

  auto the_type_name = Name::CreateDerived(library, SourceSpan(), std::string(name));
  const Type* the_type;
  ASSERT_TRUE(typespace->Create(the_type_name, nullptr /* maybe_arg_type */,
                                std::optional<types::HandleSubtype>(), nullptr /* handle_rights */,
                                nullptr /* maybe_size */, types::Nullability::kNonnullable,
                                &the_type, nullptr));
  ASSERT_NOT_NULL(the_type, "%s", name);
  auto the_type_p = static_cast<const PrimitiveType*>(the_type);
  ASSERT_EQ(the_type_p->subtype, subtype, "%s", name);
}

// Tests that we can look up root types with global names, i.e. those absent
// of any libraries.
TEST(TypesTests, root_types_with_no_library_in_lookup) {
  Typespace typespace = Typespace::RootTypes(nullptr);
  Library* library = nullptr;

  CheckPrimitiveType(library, &typespace, "bool", types::PrimitiveSubtype::kBool);
  CheckPrimitiveType(library, &typespace, "int8", types::PrimitiveSubtype::kInt8);
  CheckPrimitiveType(library, &typespace, "int16", types::PrimitiveSubtype::kInt16);
  CheckPrimitiveType(library, &typespace, "int32", types::PrimitiveSubtype::kInt32);
  CheckPrimitiveType(library, &typespace, "int64", types::PrimitiveSubtype::kInt64);
  CheckPrimitiveType(library, &typespace, "uint8", types::PrimitiveSubtype::kUint8);
  CheckPrimitiveType(library, &typespace, "uint16", types::PrimitiveSubtype::kUint16);
  CheckPrimitiveType(library, &typespace, "uint32", types::PrimitiveSubtype::kUint32);
  CheckPrimitiveType(library, &typespace, "uint64", types::PrimitiveSubtype::kUint64);
  CheckPrimitiveType(library, &typespace, "float32", types::PrimitiveSubtype::kFloat32);
  CheckPrimitiveType(library, &typespace, "float64", types::PrimitiveSubtype::kFloat64);
}

// Tests that we can look up root types with local names, i.e. those within
// the context of a library.
TEST(TypesTests, root_types_with_some_library_in_lookup) {
  Typespace typespace = Typespace::RootTypes(nullptr);

  TestLibrary library("library fidl.test;");
  ASSERT_TRUE(library.Compile());
  auto library_ptr = library.library();

  CheckPrimitiveType(library_ptr, &typespace, "bool", types::PrimitiveSubtype::kBool);
  CheckPrimitiveType(library_ptr, &typespace, "int8", types::PrimitiveSubtype::kInt8);
  CheckPrimitiveType(library_ptr, &typespace, "int16", types::PrimitiveSubtype::kInt16);
  CheckPrimitiveType(library_ptr, &typespace, "int32", types::PrimitiveSubtype::kInt32);
  CheckPrimitiveType(library_ptr, &typespace, "int64", types::PrimitiveSubtype::kInt64);
  CheckPrimitiveType(library_ptr, &typespace, "uint8", types::PrimitiveSubtype::kUint8);
  CheckPrimitiveType(library_ptr, &typespace, "uint16", types::PrimitiveSubtype::kUint16);
  CheckPrimitiveType(library_ptr, &typespace, "uint32", types::PrimitiveSubtype::kUint32);
  CheckPrimitiveType(library_ptr, &typespace, "uint64", types::PrimitiveSubtype::kUint64);
  CheckPrimitiveType(library_ptr, &typespace, "float32", types::PrimitiveSubtype::kFloat32);
  CheckPrimitiveType(library_ptr, &typespace, "float64", types::PrimitiveSubtype::kFloat64);
}

// Check that fidl's types.h and zircon/types.h's handle subtype
// values stay in sync, until the latter is generated.
TEST(TypesTests, handle_subtype) {
  static_assert(sizeof(types::HandleSubtype) == sizeof(zx_obj_type_t));

  static_assert(types::HandleSubtype::kHandle ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_NONE));

  static_assert(types::HandleSubtype::kBti == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_BTI));
  static_assert(types::HandleSubtype::kChannel ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_CHANNEL));
  static_assert(types::HandleSubtype::kClock ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_CLOCK));
  static_assert(types::HandleSubtype::kEvent ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_EVENT));
  static_assert(types::HandleSubtype::kEventpair ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_EVENTPAIR));
  static_assert(types::HandleSubtype::kException ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_EXCEPTION));
  static_assert(types::HandleSubtype::kFifo == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_FIFO));
  static_assert(types::HandleSubtype::kGuest ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_GUEST));
  static_assert(types::HandleSubtype::kInterrupt ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_INTERRUPT));
  static_assert(types::HandleSubtype::kIommu ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_IOMMU));
  static_assert(types::HandleSubtype::kJob == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_JOB));
  static_assert(types::HandleSubtype::kLog == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_LOG));
  static_assert(types::HandleSubtype::kPager ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PAGER));
  static_assert(types::HandleSubtype::kPciDevice ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PCI_DEVICE));
  static_assert(types::HandleSubtype::kPmt == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PMT));
  static_assert(types::HandleSubtype::kPort == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PORT));
  static_assert(types::HandleSubtype::kProcess ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PROCESS));
  static_assert(types::HandleSubtype::kProfile ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PROFILE));
  static_assert(types::HandleSubtype::kResource ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_RESOURCE));
  static_assert(types::HandleSubtype::kSocket ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_SOCKET));
  static_assert(types::HandleSubtype::kStream ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_STREAM));
  static_assert(types::HandleSubtype::kSuspendToken ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_SUSPEND_TOKEN));
  static_assert(types::HandleSubtype::kThread ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_THREAD));
  static_assert(types::HandleSubtype::kTimer ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_TIMER));
  static_assert(types::HandleSubtype::kVcpu == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_VCPU));
  static_assert(types::HandleSubtype::kVmar == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_VMAR));
  static_assert(types::HandleSubtype::kVmo == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_VMO));
}

// Check that fidl's types.h and zircon/types.h's rights types stay in
// sync, until the latter is generated.
TEST(TypesTests, rights) { static_assert(sizeof(types::Rights) == sizeof(zx_rights_t)); }

}  // namespace flat
}  // namespace fidl
