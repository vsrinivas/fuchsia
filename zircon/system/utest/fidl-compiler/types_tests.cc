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

}  // namespace flat
}  // namespace fidl
