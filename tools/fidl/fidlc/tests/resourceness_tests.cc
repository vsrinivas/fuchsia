// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/lexer.h"
#include "tools/fidl/fidlc/include/fidl/parser.h"
#include "tools/fidl/fidlc/include/fidl/source_file.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

void invalid_resource_modifier(const std::string& type, const std::string& definition) {
  std::string fidl_library = "library example;\n\n" + definition + "\n";

  TestLibrary library(fidl_library);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotSpecifyModifier);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "resource");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), type);
}

TEST(ResourcenessTests, BadBitsResourceness) {
  invalid_resource_modifier("bits", R"FIDL(
type Foo = resource bits {
    BAR = 0x1;
};
)FIDL");
}

TEST(ResourcenessTests, BadEnumResourceness) {
  invalid_resource_modifier("enum", R"FIDL(
type Foo = resource enum {
    BAR = 1;
};
)FIDL");
}

TEST(ResourcenessTests, BadConstResourceness) {
  TestLibrary library(R"FIDL(
library example;

resource const BAR uint32 = 1;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(ResourcenessTests, BadProtocolResourceness) {
  TestLibrary library(R"FIDL(
library example;

resource protocol Foo {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(ResourcenessTests, BadAliasResourceness) {
  TestLibrary library(R"FIDL(
library example;

resource alias B = bool;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(ResourcenessTests, BadDuplicateModifier) {
  TestLibrary library(R"FIDL(
library example;

type One = resource struct {};
type Two = resource resource struct {};            // line 5
type Three = resource resource resource struct {}; // line 6
)FIDL");
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateModifier);
  EXPECT_EQ(errors[0]->span.position().line, 5);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "resource");
  ASSERT_ERR(errors[1], fidl::ErrDuplicateModifier);
  EXPECT_EQ(errors[1]->span.position().line, 6);
  ASSERT_SUBSTR(errors[1]->msg.c_str(), "resource");
  ASSERT_ERR(errors[2], fidl::ErrDuplicateModifier);
  EXPECT_EQ(errors[2]->span.position().line, 6);
  ASSERT_SUBSTR(errors[2]->msg.c_str(), "resource");
}

TEST(ResourcenessTests, GoodResourceSimple) {
  TestLibrary library;
  library.UseLibraryZx();
  library.AddFile("good/fi-0110-a.test.fidl");

  ASSERT_COMPILED(library);
}

TEST(ResourcenessTests, BadResourceModifierMissing) {
  TestLibrary library;
  library.UseLibraryZx();
  library.AddFile("bad/fi-0110.test.fidl");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
}

TEST(ResourcenessTests, GoodResourceStruct) {
  for (const std::string& definition : {
           "type Foo =  resource struct {};",
           "type Foo = resource struct { b bool; };",
           "using zx;\ntype Foo = resource struct{ h zx.handle; };",
           "using zx;\ntype Foo = resource struct{ a array<zx.handle, 1>; };",
           "using zx;\ntype Foo = resource struct{ v vector<zx.handle>; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ASSERT_COMPILED(library);
    EXPECT_EQ(library.LookupStruct("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, GoodResourceTable) {
  for (const std::string& definition : {
           "type Foo = resource table {};",
           "type Foo = resource table { 1: b bool; };",
           "using zx;\ntype Foo = resource table { 1: h zx.handle; };",
           "using zx;\ntype Foo = resource table { 1: a array<zx.handle, 1>; };",
           "using zx;\ntype Foo = resource table { 1: v vector<zx.handle>; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ASSERT_COMPILED(library);
    EXPECT_EQ(library.LookupTable("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, GoodResourceUnion) {
  for (const std::string& definition : {
           "type Foo = resource union { 1: b bool; };",
           "using zx;\ntype Foo = resource union { 1: h zx.handle; };",
           "using zx;\ntype Foo = resource union { 1: a array<zx.handle, 1>; };",
           "using zx;\ntype Foo = resource union { 1: v vector<zx.handle>; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ;
    ASSERT_COMPILED(library);
    EXPECT_EQ(library.LookupUnion("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, BadHandlesInValueStruct) {
  for (const std::string& definition : {
           "type Foo = struct { bad_member zx.handle; };",
           "type Foo = struct { bad_member zx.handle:optional; };",
           "type Foo = struct { bad_member array<zx.handle, 1>; };",
           "type Foo = struct { bad_member vector<zx.handle>; };",
           "type Foo = struct { bad_member vector<zx.handle>:0; };",
       }) {
    std::string fidl_library = "library example;\nusing zx;\n\n" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, BadHandlesInValueTable) {
  for (const std::string& definition : {
           "type Foo = table { 1: bad_member zx.handle; };",
           "type Foo = table { 1: bad_member array<zx.handle, 1>; };",
           "type Foo = table { 1: bad_member vector<zx.handle>; };",
           "type Foo = table { 1: bad_member vector<zx.handle>:0; };",
       }) {
    std::string fidl_library = "library example;\nusing zx;\n\n" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, BadHandlesInValueUnion) {
  for (const std::string& definition : {
           "type Foo = union { 1: bad_member zx.handle; };",
           "type Foo = union { 1: bad_member array<zx.handle, 1>; };",
           "type Foo = union { 1: bad_member vector<zx.handle>; };",
           "type Foo = union { 1: bad_member vector<zx.handle>:0; };",
       }) {
    std::string fidl_library = "library example;\nusing zx;\n\n" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}

TEST(ResourcenessTests, BadProtocolsInValueType) {
  for (const std::string& definition : {
           "type Foo = struct { bad_member client_end:Protocol; };",
           "type Foo = struct { bad_member client_end:<Protocol, optional>; };",
           "type Foo = struct { bad_member server_end:Protocol; };",
           "type Foo = struct { bad_member server_end:<Protocol, optional>; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;
using zx;

protocol Protocol {};

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}
TEST(ResourcenessTests, BadResourceTypesInValueType) {
  for (const std::string& definition : {
           "type Foo = struct { bad_member ResourceStruct; };",
           "type Foo = struct { bad_member box<ResourceStruct>; };",
           "type Foo = struct { bad_member ResourceTable; };",
           "type Foo = struct { bad_member ResourceUnion; };",
           "type Foo = struct { bad_member ResourceUnion:optional; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;

type ResourceStruct = resource struct {};
type ResourceTable = resource table {};
type ResourceUnion = resource union { 1: b bool; };

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}

TEST(ResourcenessTests, BadResourceAliasesInValueType) {
  for (const std::string& definition : {
           "type Foo = struct { bad_member HandleAlias; };",
           "type Foo = struct { bad_member ProtocolAlias; };",
           "type Foo = struct { bad_member ResourceStructAlias; };",
           "type Foo = struct { bad_member ResourceTableAlias; };",
           "type Foo = struct { bad_member ResourceUnionAlias; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;
using zx;

alias HandleAlias = zx.handle;
alias ProtocolAlias = client_end:Protocol;
alias ResourceStructAlias = ResourceStruct;
alias ResourceTableAlias = ResourceStruct;
alias ResourceUnionAlias = ResourceStruct;

protocol Protocol {};
type ResourceStruct = resource struct {};
type ResourceTable = resource table {};
type ResourceUnion = resource union { 1: b bool; };

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}

TEST(ResourcenessTests, BadResourcesInNestedContainers) {
  for (const std::string& definition : {
           "type Foo = struct { bad_member vector<vector<zx.handle>>; };",
           "type Foo = struct { bad_member vector<vector<zx.handle:optional>>; };",
           "type Foo = struct { bad_member vector<vector<client_end:Protocol>>; };",
           "type Foo = struct { bad_member vector<vector<ResourceStruct>>; };",
           "type Foo = struct { bad_member vector<vector<ResourceTable>>; };",
           "type Foo = struct { bad_member vector<vector<ResourceUnion>>; };",
           "type Foo = struct { bad_member "
           "vector<array<vector<ResourceStruct>:optional,2>>:optional; };",
       }) {
    std::string fidl_library = R"FIDL(
library example;
using zx;

protocol Protocol {};
type ResourceStruct = resource struct {};
type ResourceTable = resource table {};
type ResourceUnion = resource union { 1: b bool; };

)FIDL" + definition + "\n";
    TestLibrary library(fidl_library);
    library.UseLibraryZx();
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}

TEST(ResourcenessTests, BadMultipleResourceTypesInValueType) {
  TestLibrary library(R"FIDL(
library example;
using zx;

type Foo = struct {
  first zx.handle;
  second zx.handle:optional;
  third ResourceStruct;
};

type ResourceStruct = resource struct {};
)FIDL");
  library.UseLibraryZx();
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

TEST(ResourcenessTests, GoodTransitiveResourceMember) {
  std::string fidl_library = R"FIDL(library example;

type Top = resource struct {
    middle Middle;
};
type Middle = resource struct {
    bottom Bottom;
};
type Bottom = resource struct {};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_COMPILED(library);
  EXPECT_EQ(library.LookupStruct("Top")->resourceness, fidl::types::Resourceness::kResource);
}

TEST(ResourcenessTests, BadTransitiveResourceMember) {
  TestLibrary library(R"FIDL(
library example;

type Top = struct {
  middle Middle;
};
type Middle = struct {
  bottom Bottom;
};
type Bottom = resource struct {};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeMustBeResource,
                                      fidl::ErrTypeMustBeResource);
  // `Middle` must be a resource because it includes `bottom`, a *nominal* resource.
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Middle");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bottom");

  // `Top` must be a resource because it includes `middle`, an *effective* resource.
  ASSERT_SUBSTR(library.errors()[1]->msg.c_str(), "Top");
  ASSERT_SUBSTR(library.errors()[1]->msg.c_str(), "middle");
}

TEST(ResourcenessTests, GoodRecursiveValueTypes) {
  std::string fidl_library = R"FIDL(library example;

type Ouro = struct {
    b box<Boros>;
};

type Boros = struct {
    o box<Ouro>;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_COMPILED(library);
}

TEST(ResourcenessTests, GoodRecursiveResourceTypes) {
  std::string fidl_library = R"FIDL(library example;

type Ouro = resource struct {
    b box<Boros>;
};

type Boros = resource struct {
    o box<Ouro>;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_COMPILED(library);
}

TEST(ResourcenessTests, BadRecursiveResourceTypes) {
  TestLibrary library(R"FIDL(
library example;

type Ouro = resource struct {
  b box<Boros>;
};

type Boros = struct {
  bad_member box<Ouro>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Boros");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member");
}

TEST(ResourcenessTests, GoodStrictResourceOrderIndependent) {
  TestLibrary library(R"FIDL(library example;

type SR = strict resource union {
    1: b bool;
};
type RS = strict resource union {
    1: b bool;
};
)FIDL");
  ASSERT_COMPILED(library);

  const auto strict_resource = library.LookupUnion("SR");
  EXPECT_EQ(strict_resource->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(strict_resource->resourceness, fidl::types::Resourceness::kResource);

  const auto resource_strict = library.LookupUnion("RS");
  EXPECT_EQ(resource_strict->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(resource_strict->resourceness, fidl::types::Resourceness::kResource);
}

}  // namespace
