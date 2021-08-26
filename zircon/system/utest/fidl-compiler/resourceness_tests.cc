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

void invalid_resource_modifier(const std::string& type, const std::string& definition) {
  std::string fidl_library = "library example;\n\n" + definition + "\n";

  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(fidl_library, experimental_flags);
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

// NOTE(fxbug.dev/72924): we don't parse resource in this position in the
// new syntax.
TEST(ResourcenessTests, BadConstResourceness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

resource const BAR uint32 = 1;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

// NOTE(fxbug.dev/72924): we don't parse resource in this position in the
// new syntax.
TEST(ResourcenessTests, BadProtocolResourceness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

resource protocol Foo {};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

// NOTE(fxbug.dev/72924): we don't parse resource in this position in the
// new syntax.
TEST(ResourcenessTests, BadAliasResourceness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

resource alias B = bool;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(ResourcenessTests, BadDuplicateModifier) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type One = resource struct {};
type Two = resource resource struct {};            // line 5
type Three = resource resource resource struct {}; // line 6
  )FIDL",
                      experimental_flags);
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateModifier);
  EXPECT_EQ(errors[0]->span->position().line, 5);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "resource");
  ASSERT_ERR(errors[1], fidl::ErrDuplicateModifier);
  EXPECT_EQ(errors[1]->span->position().line, 6);
  ASSERT_SUBSTR(errors[1]->msg.c_str(), "resource");
  ASSERT_ERR(errors[2], fidl::ErrDuplicateModifier);
  EXPECT_EQ(errors[2]->span->position().line, 6);
  ASSERT_SUBSTR(errors[2]->msg.c_str(), "resource");
}

TEST(ResourcenessTests, GoodResourceStruct) {
  for (const std::string& definition : {
           "resource struct Foo {};",
           "resource struct Foo { bool b; };",
           "using zx;\nresource struct Foo { zx.handle h; };",
           "using zx;\nresource struct Foo { array<zx.handle>:1 a; };",
           "using zx;\nresource struct Foo { vector<zx.handle> v; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    auto library = WithLibraryZx(fidl_library);
    ASSERT_COMPILED_AND_CONVERT(library);
    EXPECT_EQ(library.LookupStruct("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, GoodResourceTable) {
  for (const std::string& definition : {
           "resource table Foo {};",
           "resource table Foo { 1: bool b; };",
           "using zx;\nresource table Foo { 1: zx.handle h; };",
           "using zx;\nresource table Foo { 1: array<zx.handle>:1 a; };",
           "using zx;\nresource table Foo { 1: vector<zx.handle> v; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    auto library = WithLibraryZx(fidl_library);
    ASSERT_COMPILED_AND_CONVERT(library);
    EXPECT_EQ(library.LookupTable("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, GoodResourceUnion) {
  for (const std::string& definition : {
           "resource union Foo { 1: bool b; };",
           "using zx;\nresource union Foo { 1: zx.handle h; };",
           "using zx;\nresource union Foo { 1: array<zx.handle>:1 a; };",
           "using zx;\nresource union Foo { 1: vector<zx.handle> v; };",
       }) {
    std::string fidl_library = "library example;\n\n" + definition + "\n";
    auto library = WithLibraryZx(fidl_library);
    ;
    ASSERT_COMPILED_AND_CONVERT(library);
    EXPECT_EQ(library.LookupUnion("Foo")->resourceness, fidl::types::Resourceness::kResource, "%s",
              fidl_library.c_str());
  }
}

TEST(ResourcenessTests, BadHandlesInValueStruct) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  for (const std::string& definition : {
           "type Foo = struct { bad_member zx.handle; };",
           "type Foo = struct { bad_member zx.handle:optional; };",
           "type Foo = struct { bad_member array<zx.handle, 1>; };",
           "type Foo = struct { bad_member vector<zx.handle>; };",
           "type Foo = struct { bad_member vector<zx.handle>:0; };",
       }) {
    std::string fidl_library = "library example;\nusing zx;\n\n" + definition + "\n";
    auto library = WithLibraryZx(fidl_library, experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, BadHandlesInValueTable) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  for (const std::string& definition : {
           "type Foo = table { 1: bad_member zx.handle; };",
           "type Foo = table { 1: bad_member array<zx.handle, 1>; };",
           "type Foo = table { 1: bad_member vector<zx.handle>; };",
           "type Foo = table { 1: bad_member vector<zx.handle>:0; };",
       }) {
    std::string fidl_library = "library example;\nusing zx;\n\n" + definition + "\n";
    auto library = WithLibraryZx(fidl_library, experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
  }
}

TEST(ResourcenessTests, BadHandlesInValueUnion) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  for (const std::string& definition : {
           "type Foo = union { 1: bad_member zx.handle; };",
           "type Foo = union { 1: bad_member array<zx.handle, 1>; };",
           "type Foo = union { 1: bad_member vector<zx.handle>; };",
           "type Foo = union { 1: bad_member vector<zx.handle>:0; };",
       }) {
    std::string fidl_library = "library example;\nusing zx;\n\n" + definition + "\n";
    auto library = WithLibraryZx(fidl_library, experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}

TEST(ResourcenessTests, BadProtocolsInValueType) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
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
    auto library = WithLibraryZx(fidl_library, experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}
TEST(ResourcenessTests, BadResourceTypesInValueType) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  for (const std::string& definition : {
           "type Foo = struct { bad_member ResourceStruct; };",
           "type Foo = struct { bad_member ResourceStruct:optional; };",
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
    auto library = WithLibraryZx(fidl_library, experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}

TEST(ResourcenessTests, BadResourceAliasesInValueType) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
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
    auto library = WithLibraryZx(fidl_library, experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}

TEST(ResourcenessTests, BadResourcesInNestedContainers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
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
    auto library = WithLibraryZx(fidl_library, experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Foo", "%s", fidl_library.c_str());
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member", "%s", fidl_library.c_str());
    ;
  }
}

TEST(ResourcenessTests, BadMultipleResourceTypesInValueType) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  auto library = WithLibraryZx(R"FIDL(
library example;
using zx;

type Foo = struct {
  first zx.handle;
  second zx.handle:optional;
  third ResourceStruct;
};

type ResourceStruct = resource struct {};
)FIDL",
                               experimental_flags);
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

  TestLibrary library(fidl_library);
  ASSERT_COMPILED_AND_CONVERT(library);
  EXPECT_EQ(library.LookupStruct("Top")->resourceness, fidl::types::Resourceness::kResource);
}

TEST(ResourcenessTests, BadTransitiveResourceMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Top = struct {
  middle Middle;
};
type Middle = struct {
  bottom Bottom;
};
type Bottom = resource struct {};
)FIDL",
                      experimental_flags);
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
  std::string fidl_library = R"FIDL(
library example;

struct Ouro {
  Boros? b;
};

struct Boros {
  Ouro? o;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ResourcenessTests, GoodRecursiveResourceTypes) {
  std::string fidl_library = R"FIDL(
library example;

resource struct Ouro {
  Boros? b;
};

resource struct Boros {
  Ouro? o;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ResourcenessTests, BadRecursiveResourceTypes) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Ouro = resource struct {
  b Boros:optional;
};

type Boros = struct {
  bad_member Ouro:optional;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Boros");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bad_member");
}

TEST(ResourcenessTests, GoodStrictResourceOrderIndependent) {
  TestLibrary library(R"FIDL(
library example;

strict resource union SR { 1: bool b; };
resource strict union RS { 1: bool b; };
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  const auto strict_resource = library.LookupUnion("SR");
  EXPECT_EQ(strict_resource->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(strict_resource->resourceness, fidl::types::Resourceness::kResource);

  const auto resource_strict = library.LookupUnion("RS");
  EXPECT_EQ(resource_strict->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(resource_strict->resourceness, fidl::types::Resourceness::kResource);
}

}  // namespace
