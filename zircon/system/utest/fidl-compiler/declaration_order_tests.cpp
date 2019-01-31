// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

#define ASSERT_DECL_NAME(D, N) \
    ASSERT_STR_EQ(N, static_cast<const std::string>(D->name.name_part()).c_str());

namespace {

bool nonnullable_ref() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

struct TheRequestStruct_02 {
  array<TheElementStruct_03>:4 req;
};

struct TheElementStruct_03 {};

interface TheInterface_01 {
  SomeMethod(TheRequestStruct_02 req);
};

)FIDL");
    ASSERT_TRUE(library.Compile());
    auto decl_order = library.declaration_order();
    ASSERT_EQ(4, decl_order.size());
    ASSERT_DECL_NAME(decl_order[0], "TheElementStruct_03");
    ASSERT_DECL_NAME(decl_order[1], "TheRequestStruct_02");
    ASSERT_DECL_NAME(decl_order[2], "SomeLongAnonymousPrefix0");
    ASSERT_DECL_NAME(decl_order[3], "TheInterface_01");

    END_TEST;
}

bool nullable_ref_breaks_dependency() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

struct TheRequestStruct_02 {
  array<TheElementStruct_03?>:4 req;
};

struct TheElementStruct_03 {};

interface TheInterface_01 {
  SomeMethod(TheRequestStruct_02 req);
};

)FIDL");
    ASSERT_TRUE(library.Compile());
    auto decl_order = library.declaration_order();
    ASSERT_EQ(4, decl_order.size());
    ASSERT_DECL_NAME(decl_order[0], "TheRequestStruct_02");
    ASSERT_DECL_NAME(decl_order[1], "SomeLongAnonymousPrefix0");
    ASSERT_DECL_NAME(decl_order[2], "TheInterface_01");
    ASSERT_DECL_NAME(decl_order[3], "TheElementStruct_03");

    END_TEST;
}

bool request_type_breaks_dependency_graph() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

struct TheRequestStruct_02 {
  request<TheInterface_01> req;
};

interface TheInterface_01 {
  SomeMethod(TheRequestStruct_02 req);
};

)FIDL");
    ASSERT_TRUE(library.Compile());
    auto decl_order = library.declaration_order();
    ASSERT_EQ(3, decl_order.size());
    ASSERT_DECL_NAME(decl_order[0], "TheRequestStruct_02");
    ASSERT_DECL_NAME(decl_order[1], "SomeLongAnonymousPrefix0");
    ASSERT_DECL_NAME(decl_order[2], "TheInterface_01");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(declaration_order_test);
RUN_TEST(nonnullable_ref);
RUN_TEST(nullable_ref_breaks_dependency);
RUN_TEST(request_type_breaks_dependency_graph);
END_TEST_CASE(declaration_order_test);
