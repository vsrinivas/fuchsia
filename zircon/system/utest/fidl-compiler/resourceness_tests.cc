// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "test_library.h"

namespace {

const fidl::ExperimentalFlags FLAGS(fidl::ExperimentalFlags::Flag::kDefaultNoHandles);

void invalid_resource_modifier(const std::string& definition) {
  std::string fidl_library = "library example;\n\n" + definition + "\n";

  TestLibrary library(fidl_library, FLAGS);
  library.set_warnings_as_errors(true);
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
    library.set_warnings_as_errors(true);
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
    library.set_warnings_as_errors(true);
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
    library.set_warnings_as_errors(true);
    ASSERT_TRUE(library.Compile(), "%s", fidl_library.c_str());
    EXPECT_EQ(library.LookupUnion("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_handles_in_value_struct) {
  for (const std::string& definition : {
           "struct Foo { handle bad_member; };",
           "struct Foo { handle? bad_member; };",
           "struct Foo { array<handle>:1 bad_member; };",
           "struct Foo { vector<handle> bad_member; };",
           "struct Foo { vector<handle>:0 bad_member; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    library.set_warnings_as_errors(true);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource, "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_handles_in_value_table) {
  for (const std::string& definition : {
           "table Foo { 1: handle bad_member; };",
           "table Foo { 1: array<handle>:1 bad_member; };",
           "table Foo { 1: vector<handle> bad_member; };",
           "table Foo { 1: vector<handle>:0 bad_member; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    library.set_warnings_as_errors(true);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource, "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_handles_in_value_union) {
  for (const std::string& definition : {
           "union Foo { 1: handle bad_member; };",
           "union Foo { 1: array<handle>:1 bad_member; };",
           "union Foo { 1: vector<handle> bad_member; };",
           "union Foo { 1: vector<handle>:0 bad_member; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    library.set_warnings_as_errors(true);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource, "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_protocols_in_value_type) {
  for (const std::string& definition : {
           "struct Foo { Protocol bad_member; };",
           "struct Foo { Protocol? bad_member; };",
           "struct Foo { request<Protocol> bad_member; };",
           "struct Foo { request<Protocol>? bad_member; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;

protocol Protocol {};

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    library.set_warnings_as_errors(true);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource, "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_resource_types_in_value_type) {
  for (const std::string& definition : {
           "struct Foo { ResourceStruct bad_member; };",
           "struct Foo { ResourceStruct? bad_member; };",
           "struct Foo { ResourceTable bad_member; };",
           "struct Foo { ResourceUnion bad_member; };",
           "struct Foo { ResourceUnion? bad_member; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;

resource struct ResourceStruct {};
resource table ResourceTable {};
resource union ResourceUnion { 1: bool b; };

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    library.set_warnings_as_errors(true);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource, "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_resource_aliases_in_value_type) {
  for (const std::string& definition : {
           "struct Foo { HandleAlias bad_member; };",
           "struct Foo { ProtocolAlias bad_member; };",
           "struct Foo { ResourceStructAlias bad_member; };",
           "struct Foo { ResourceTableAlias bad_member; };",
           "struct Foo { ResourceUnionAlias bad_member; };",
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
    library.set_warnings_as_errors(true);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource, "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_resources_in_nested_containers) {
  for (const std::string& definition : {
           "struct Foo { vector<vector<handle>> bad_member; };",
           "struct Foo { vector<vector<handle?>> bad_member; };",
           "struct Foo { vector<vector<Protocol>> bad_member; };",
           "struct Foo { vector<vector<ResourceStruct>> bad_member; };",
           "struct Foo { vector<vector<ResourceTable>> bad_member; };",
           "struct Foo { vector<vector<ResourceUnion>> bad_member; };",
           "struct Foo { vector<array<vector<ResourceStruct>?>:2>? bad_member; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;

protocol Protocol {};
resource struct ResourceStruct {};
resource table ResourceTable {};
resource union ResourceUnion { 1: bool b; };

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library, FLAGS);
    library.set_warnings_as_errors(true);
    ASSERT_FALSE(library.Compile(), "%s", fidl_library.c_str());

    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 1, "%s", fidl_library.c_str());
    ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource, "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(errors[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, bad_multiple_resource_types_in_value_type) {
  std::string fidl_library = R"FIDL(
library example;

struct Foo {
  handle first;
  handle? second;
  ResourceStruct third;
};

resource struct ResourceStruct {};
)FIDL";

  TestLibrary library(fidl_library, FLAGS);
  library.set_warnings_as_errors(true);
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);

  ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "Foo");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "first");

  ASSERT_ERR(errors[1], fidl::ErrTypeMustBeResource);
  ASSERT_SUBSTR(errors[1]->msg.c_str(), "Foo");
  ASSERT_SUBSTR(errors[1]->msg.c_str(), "second");

  ASSERT_ERR(errors[2], fidl::ErrTypeMustBeResource);
  ASSERT_SUBSTR(errors[2]->msg.c_str(), "Foo");
  ASSERT_SUBSTR(errors[2]->msg.c_str(), "third");
}

TEST(ResourcenessTests, good_transitive_resource_member) {
  std::string fidl_library = R"FIDL(
library example;

resource struct Top {
  Middle middle;
};
resource struct Middle {
  Bottom bottom;
};
resource struct Bottom {};
)FIDL";

  TestLibrary library(fidl_library, FLAGS);
  library.set_warnings_as_errors(true);
  ASSERT_TRUE(library.Compile());
  EXPECT_EQ(library.LookupStruct("Top")->resourceness, fidl::types::Resourceness::kResource);
}

TEST(ResourcenessTests, bad_transitive_resource_member) {
  std::string fidl_library = R"FIDL(
library example;

struct Top {
  Middle middle;
};
struct Middle {
  Bottom bottom;
};
resource struct Bottom {};
)FIDL";

  TestLibrary library(fidl_library, FLAGS);
  library.set_warnings_as_errors(true);
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);

  // `Middle` must be a resource because it includes `bottom`, a *nominal* resource.
  ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "Middle");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "bottom");

  // `Top` must be a resource because it includes `middle`, an *effective* resource.
  ASSERT_ERR(errors[1], fidl::ErrTypeMustBeResource);
  ASSERT_SUBSTR(errors[1]->msg.c_str(), "Top");
  ASSERT_SUBSTR(errors[1]->msg.c_str(), "middle");
}

TEST(ResourcenessTests, good_recursive_value_types) {
  std::string fidl_library = R"FIDL(
library example;

struct Ouro {
  Boros? b;
};

struct Boros {
  Ouro? o;
};
)FIDL";

  TestLibrary library(fidl_library, FLAGS);
  library.set_warnings_as_errors(true);
  ASSERT_TRUE(library.Compile());
}

TEST(ResourcenessTests, good_recursive_resource_types) {
  std::string fidl_library = R"FIDL(
library example;

resource struct Ouro {
  Boros? b;
};

resource struct Boros {
  Ouro? o;
};
)FIDL";

  TestLibrary library(fidl_library, FLAGS);
  library.set_warnings_as_errors(true);
  ASSERT_TRUE(library.Compile());
}

TEST(ResourcenessTests, bad_recursive_resource_types) {
  std::string fidl_library = R"FIDL(
library example;

resource struct Ouro {
  Boros? b;
};

struct Boros {
  Ouro? bad_member;
};
)FIDL";

  TestLibrary library(fidl_library, FLAGS);
  library.set_warnings_as_errors(true);
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);

  ASSERT_ERR(errors[0], fidl::ErrTypeMustBeResource);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "Boros");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "bad_member");
}

}  // namespace
