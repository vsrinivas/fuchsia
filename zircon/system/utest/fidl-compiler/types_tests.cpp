// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>

#include "test_library.h"

namespace fidl {
namespace flat {

#define CHECK_PRIMITIVE_TYPE(N, S)                                     \
    {                                                                  \
        auto the_type_name = Name(library_ptr, N);                     \
        const Type* the_type;                                          \
        ASSERT_TRUE(typespace.Create(                                  \
            the_type_name,                                             \
            nullptr /* maybe_arg_type */,                              \
            nullptr /* maybe_handle_subtype */,                        \
            nullptr /* maybe_size */,                                  \
            types::Nullability::kNonnullable,                          \
            &the_type));                                               \
        ASSERT_NE(the_type, nullptr, N);                               \
        auto the_type_p = static_cast<const PrimitiveType*>(the_type); \
        ASSERT_EQ(the_type_p->subtype, S, N);                          \
    }

// Tests that we can look up root types with global names, i.e. those absent
// of any libraries.
bool root_types_with_no_library_in_lookup() {
    BEGIN_TEST;

    Typespace typespace = Typespace::RootTypes(nullptr);

    Library* library_ptr = nullptr;

    CHECK_PRIMITIVE_TYPE("bool", types::PrimitiveSubtype::kBool);
    CHECK_PRIMITIVE_TYPE("int8", types::PrimitiveSubtype::kInt8);
    CHECK_PRIMITIVE_TYPE("int16", types::PrimitiveSubtype::kInt16);
    CHECK_PRIMITIVE_TYPE("int32", types::PrimitiveSubtype::kInt32);
    CHECK_PRIMITIVE_TYPE("int64", types::PrimitiveSubtype::kInt64);
    CHECK_PRIMITIVE_TYPE("uint8", types::PrimitiveSubtype::kUint8);
    CHECK_PRIMITIVE_TYPE("uint16", types::PrimitiveSubtype::kUint16);
    CHECK_PRIMITIVE_TYPE("uint32", types::PrimitiveSubtype::kUint32);
    CHECK_PRIMITIVE_TYPE("uint64", types::PrimitiveSubtype::kUint64);
    CHECK_PRIMITIVE_TYPE("float32", types::PrimitiveSubtype::kFloat32);
    CHECK_PRIMITIVE_TYPE("float64", types::PrimitiveSubtype::kFloat64);

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

    CHECK_PRIMITIVE_TYPE("bool", types::PrimitiveSubtype::kBool);
    CHECK_PRIMITIVE_TYPE("int8", types::PrimitiveSubtype::kInt8);
    CHECK_PRIMITIVE_TYPE("int16", types::PrimitiveSubtype::kInt16);
    CHECK_PRIMITIVE_TYPE("int32", types::PrimitiveSubtype::kInt32);
    CHECK_PRIMITIVE_TYPE("int64", types::PrimitiveSubtype::kInt64);
    CHECK_PRIMITIVE_TYPE("uint8", types::PrimitiveSubtype::kUint8);
    CHECK_PRIMITIVE_TYPE("uint16", types::PrimitiveSubtype::kUint16);
    CHECK_PRIMITIVE_TYPE("uint32", types::PrimitiveSubtype::kUint32);
    CHECK_PRIMITIVE_TYPE("uint64", types::PrimitiveSubtype::kUint64);
    CHECK_PRIMITIVE_TYPE("float32", types::PrimitiveSubtype::kFloat32);
    CHECK_PRIMITIVE_TYPE("float64", types::PrimitiveSubtype::kFloat64);

    END_TEST;
}

BEGIN_TEST_CASE(types_tests)
RUN_TEST(root_types_with_no_library_in_lookup)
RUN_TEST(root_types_with_some_library_in_lookup)
END_TEST_CASE(types_tests)

} // namespace flat
} // namespace fidl
