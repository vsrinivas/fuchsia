// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

const fidl::ExperimentalFlags FLAGS(fidl::ExperimentalFlags::Flag::kDefaultNoHandles);

void invalid_resource_modifier(const std::string& definition) {
  std::string fidl_library = "library example;\n\n" + definition + "\n";

  TestLibrary library(fidl_library, FLAGS);
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotSpecifyResource);
}

TEST(ResourcenessTests, bad_bits_resourceness) {
  invalid_resource_modifier(R"FIDL(
resource bits Foo {
    BAR = 0x1;
};
)FIDL");
}

TEST(ResourcenessTests, bad_enum_resourceness) {
  invalid_resource_modifier(R"FIDL(
resource enum Foo {
    BAR = 1;
};
)FIDL");
}

TEST(ResourcenessTests, bad_const_resourceness) {
  invalid_resource_modifier(R"FIDL(
resource const uint32 BAR = 1;
)FIDL");
}

TEST(ResourcenessTests, bad_protocol_resourceness) {
  invalid_resource_modifier(R"FIDL(
resource protocol Foo {};
)FIDL");
}

TEST(ResourcenessTests, bad_using_resourceness) {
  invalid_resource_modifier(R"FIDL(
resource using B = bool;
)FIDL");
}

TEST(ResourcenessTests, good_resource_struct) {
  for (const std::string& definition : {
           "resource struct Foo {};",
           "resource struct Foo { bool b; };",
           "resource struct Foo { handle h; };",
           "resource struct Foo { array<handle>:1 a; };",
           "resource struct Foo { vector<handle> v; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_TRUE(library.Compile(), "%s", fidl_library.c_str());
    EXPECT_EQ(library.LookupStruct("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, good_resource_table) {
  for (const std::string& definition : {
           "resource table Foo {};",
           "resource table Foo { 1: bool b; };",
           "resource table Foo { 1: handle h; };",
           "resource table Foo { 1: array<handle>:1 a; };",
           "resource table Foo { 1: vector<handle> v; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_TRUE(library.Compile(), "%s", fidl_library.c_str());
    EXPECT_EQ(library.LookupTable("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, good_resource_union) {
  for (const std::string& definition : {
           "resource union Foo { 1: bool b; };",
           "resource union Foo { 1: handle h; };",
           "resource union Foo { 1: array<handle>:1 a; };",
           "resource union Foo { 1: vector<handle> v; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_TRUE(library.Compile(), "%s", fidl_library.c_str());
    EXPECT_EQ(library.LookupUnion("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_handles_in_value_struct) {
  for (const std::string& definition : {
           "struct Foo { handle h; };",
           "struct Foo { handle? h; };",
           "struct Foo { array<handle>:1 a; };",
           "struct Foo { vector<handle> v; };",
           "struct Foo { vector<handle>:0 v; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrResourceTypeInValueType, "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_handles_in_value_table) {
  for (const std::string& definition : {
           "table Foo { 1: handle h; };",
           "table Foo { 1: array<handle>:1 a; };",
           "table Foo { 1: vector<handle> v; };",
           "table Foo { 1: vector<handle>:0 v; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrResourceTypeInValueType, "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_handles_in_value_union) {
  for (const std::string& definition : {
           "union Foo { 1: handle h; };",
           "union Foo { 1: array<handle>:1 a; };",
           "union Foo { 1: vector<handle> v; };",
           "union Foo { 1: vector<handle>:0 v; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrResourceTypeInValueType, "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_protocols_in_value_type) {
  for (const std::string& definition : {
           "struct Foo { Protocol p; };",
           "struct Foo { Protocol? p; };",
           "struct Foo { request<Protocol> p; };",
           "struct Foo { request<Protocol>? p; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;

protocol Protocol {};

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrResourceTypeInValueType, "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_resource_types_in_value_type) {
  for (const std::string& definition : {
           "struct Foo { ResourceStruct s; };",
           "struct Foo { ResourceStruct? s; };",
           "struct Foo { ResourceTable t; };",
           "struct Foo { ResourceUnion t; };",
           "struct Foo { ResourceUnion? u; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;

resource struct ResourceStruct {};
resource table ResourceTable {};
resource union ResourceUnion { 1: bool b; };

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrResourceTypeInValueType, "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_resource_aliases_in_value_type) {
  for (const std::string& definition : {
           "struct Foo { HandleAlias h; };",
           "struct Foo { ProtocolAlias p; };",
           "struct Foo { ResourceStructAlias s; };",
           "struct Foo { ResourceTableAlias t; };",
           "struct Foo { ResourceUnionAlias u; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;

using HandleAlias = handle;
using ProtocolAlias = Protocol;
using ResourceStructAlias = ResourceStruct;
using ResourceTableAlias = ResourceStruct;
using ResourceUnionAlias = ResourceStruct;

protocol Protocol {};
resource struct ResourceStruct {};
resource table ResourceTable {};
resource union ResourceUnion { 1: bool b; };

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrResourceTypeInValueType, "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_resources_in_nested_containers) {
  for (const std::string& definition : {
           "struct Foo { vector<vector<handle>> v; };",
           "struct Foo { vector<vector<handle?>> v; };",
           "struct Foo { vector<vector<Protocol>> v; };",
           "struct Foo { vector<vector<ResourceStruct>> v; };",
           "struct Foo { vector<vector<ResourceTable>> v; };",
           "struct Foo { vector<vector<ResourceUnion>> v; };",
           "struct Foo { vector<array<vector<ResourceStruct>?>:2>? v; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;

protocol Protocol {};
resource struct ResourceStruct {};
resource table ResourceTable {};
resource union ResourceUnion { 1: bool b; };

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrResourceTypeInValueType, "%s", fidl_library.c_str());
  }
}

}  // namespace
