// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/types.h>
#include <unittest/unittest.h>

#include "test_library.h"

namespace fidl {
namespace flat {

bool CheckPrimitiveType(const Library* library, Typespace* typespace, const char* name,
                        types::PrimitiveSubtype subtype) {
  ASSERT_NONNULL(typespace);

  std::string owned_name = std::string(name);

  auto the_type_name = Name(library, owned_name);
  const Type* the_type;
  ASSERT_TRUE(typespace->Create(the_type_name, nullptr /* maybe_arg_type */,
                                std::optional<types::HandleSubtype>(), nullptr /* maybe_size */,
                                types::Nullability::kNonnullable, &the_type, nullptr));
  ASSERT_NONNULL(the_type, name);
  auto the_type_p = static_cast<const PrimitiveType*>(the_type);
  ASSERT_EQ(the_type_p->subtype, subtype, name);

  return true;
}

// Tests that we can look up root types with global names, i.e. those absent
// of any libraries.
bool root_types_with_no_library_in_lookup() {
  BEGIN_TEST;

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

  END_TEST;
}

// Tests that we can look up root types with local names, i.e. those within
// the context of a library.
bool root_types_with_some_library_in_lookup() {
  BEGIN_TEST;

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

  END_TEST;
}

BEGIN_TEST_CASE(types_tests)
RUN_TEST(root_types_with_no_library_in_lookup)
RUN_TEST(root_types_with_some_library_in_lookup)
END_TEST_CASE(types_tests)

}  // namespace flat
}  // namespace fidl
