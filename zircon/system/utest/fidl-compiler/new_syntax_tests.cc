// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/70186): Remove this file

// NOTE: this file contains unittests for the "read side" of the new syntax.
// Once the read functionality catches up to the write functionality (fidlconv),
// these tests should look to be merged with the converter tests so that each
// test case both produces new syntax and ensures that it compiles and has
// IR and coding tables that match the output from compiling the old syntax.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

// Tests that use handles need to define a "fake" zx dependency, written in the
// old syntax. This helper function streamlines that process. It also serves as
// a good psuedo-test for situations where a library written in the new syntax
// depends on one written in the old.
TestLibrary with_fake_zx(const std::string& in, SharedAmongstLibraries& shared,
                         fidl::ExperimentalFlags flags) {
  TestLibrary main_lib("example.fidl", in, &shared, flags);
  fidl::ExperimentalFlags zx_flags;
  zx_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  zx_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  std::string zx = R"FIDL(
deprecated_syntax;
library zx;

enum obj_type : uint32 {
    NONE = 0;
    PROCESS = 1;
    THREAD = 2;
    VMO = 3;
    CHANNEL = 4;
    EVENT = 5;
    PORT = 6;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};
)FIDL";
  TestLibrary zx_lib("zx.fidl", zx, &shared, zx_flags);
  zx_lib.Compile();
  main_lib.AddDependentLibrary(std::move(zx_lib));
  return main_lib;
}

TEST(NewSyntaxTests, SyntaxVersionOmitted) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type S = struct{};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED(library);
}

TEST(NewSyntaxTests, SyntaxVersionOmittedMismatch) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
deprecated_syntax;
library example;

type S = struct{};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_FALSE(library.Compile());
}

TEST(NewSyntaxTests, SyntaxVersionDeprecated) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
deprecated_syntax;
library example;

struct S {};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED(library);
}

TEST(NewSyntaxTests, SyntaxVersionDeprecatedMismatch) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
deprecated_syntax;
library example;

type S = struct{};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_FALSE(library.Compile());
}

TEST(NewSyntaxTests, SyntaxVersionMismatch) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

struct S {};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_FALSE(library.Compile());
}

TEST(NewSyntaxTests, SyntaxVersionWithoutFlag) {
  TestLibrary library(R"FIDL(
deprecated_syntax;
library example;
)FIDL");

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  EXPECT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrRemoveSyntaxVersion);
}

TEST(NewSyntaxTests, SyntaxVersionMisplaced) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
deprecated_syntax;
)FIDL",
                      std::move(experimental_flags));

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  EXPECT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMisplacedSyntaxVersion);
}

TEST(NewSyntaxTests, SyntaxVersionMisplacedWithoutFlag) {
  TestLibrary library(R"FIDL(
library example;
deprecated_syntax;
)FIDL");

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  EXPECT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrRemoveSyntaxVersion);
}

TEST(NewSyntaxTests, SyntaxVersionRepeated) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
deprecated_syntax;
library example;
deprecated_syntax;
)FIDL",
                      std::move(experimental_flags));

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  EXPECT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMisplacedSyntaxVersion);
}

TEST(NewSyntaxTests, TypeDeclOfStructLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = struct {
    field1 uint16;
    field2 uint16;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
}

TEST(NewSyntaxTests, TypeDeclOfUnionLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = union {
    1: variant1 uint16;
    2: variant2 uint16;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupUnion("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
}

TEST(NewSyntaxTests, TypeDeclOfStructLayoutWithResourceness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;

  auto library = with_fake_zx(R"FIDL(
library example;
using zx;
type t1 = struct {
    f1 uint8;
};
type t2 = resource struct {
    f1 zx.handle;
};
)FIDL",
                              shared, std::move(experimental_flags));

  ASSERT_COMPILED(library);

  auto type_decl = library.LookupStruct("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kValue);

  type_decl = library.LookupStruct("t2");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kResource);
}

TEST(NewSyntaxTests, TypeDeclOfUnionLayoutWithResourceness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;

  auto library = with_fake_zx(R"FIDL(
library example;
using zx;
type t1 = union {
    1: v1 uint8;
};
type t2 = resource union {
    1: v1 zx.handle;
};
)FIDL",
                              shared, std::move(experimental_flags));

  ASSERT_COMPILED(library);

  auto type_decl = library.LookupUnion("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kValue);

  type_decl = library.LookupUnion("t2");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kResource);
}

TEST(NewSyntaxTests, TypeDeclOfUnionLayoutWithStrictnesss) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;
type t1 = union {
    1: v1 uint8;
};
type t2 = flexible union {
    1: v1 uint8;
};
type t3 = strict union {
    1: v1 uint8;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED(library);

  auto type_decl = library.LookupUnion("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kValue);

  type_decl = library.LookupUnion("t2");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kValue);

  type_decl = library.LookupUnion("t3");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kValue);
}

TEST(NewSyntaxTests, TypeDeclOfUnionLayoutWithResourcenessAndStrictness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;

  auto library = with_fake_zx(R"FIDL(
library example;
using zx;
type t1 = resource flexible union {
    1: v1 zx.handle;
};
type t2 = flexible resource union {
    1: v1 zx.handle;
};
type t3 = resource strict union {
    1: v1 zx.handle;
};
type t4 = strict resource union {
    1: v1 zx.handle;
};
)FIDL",
                              shared, std::move(experimental_flags));

  ASSERT_COMPILED(library);

  auto type_decl = library.LookupUnion("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kResource);

  type_decl = library.LookupUnion("t2");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kResource);

  type_decl = library.LookupUnion("t3");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kResource);

  type_decl = library.LookupUnion("t4");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kResource);
}

TEST(NewSyntaxTests, TypeDeclDisallowPartialModifiers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type t1 = union{};
type t2 = strict t1;
)FIDL",
                      std::move(experimental_flags));

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  EXPECT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotSpecifyModifier);
}

TEST(NewSyntaxTests, TypeDeclOfStructLayoutWithAnonymousStruct) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = struct {
    field1 struct {
      data vector<uint8>;
    };
    field2 uint16;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
  auto type_decl_field1 = library.LookupStruct("TypeDeclField1");
  ASSERT_NOT_NULL(type_decl_field1);
  EXPECT_EQ(type_decl_field1->members.size(), 1);
}

TEST(NewSyntaxTests, LayoutMemberConstraints) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;

  // TODO(fxbug.dev/65978): a number of fields in this struct declaration have
  //  been commented out until their respective features (client/server_end)
  //  have been added to the compiler.
  auto library = with_fake_zx(R"FIDL(
library example;
using zx;
type t1 = resource struct {
  h0 zx.handle;
  h1 zx.handle:optional;
  h2 zx.handle:VMO;
  h3 zx.handle:zx.READ;
  h4 zx.handle:[VMO,optional];
  h5 zx.handle:[zx.READ,optional];
  h6 zx.handle:[VMO,zx.READ];
  h7 zx.handle:[VMO,zx.READ,optional];
  u8 union { 1: b bool; };
  u9 union { 1: b bool; }:optional;
  v10 vector<bool>;
  v11 vector<bool>:optional;
  v12 vector<bool>:16;
  v13 vector<bool>:[16,optional];
  //p14 client_end:MyProtocol;
  //p15 client_end:[MyProtocol,optional];
  //r16 server_end:P;
  //r17 server_end:[MyProtocol,optional];
};
)FIDL",
                              shared, std::move(experimental_flags));
  ASSERT_COMPILED(library);

  auto type_decl = library.LookupStruct("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 14);
  // TODO(fxbug.dev/65978): check that the flat AST has proper representation of
  //  each member's constraints. This is blocked on implementing compilation of
  //  the new constraints in the flat AST.
}

// This test ensures that recoverable parsing works as intended for constraints,
// and returns useful and actionable information back to users.
TEST(NewSyntaxTests, ConstraintsRecoverability) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;
type TypeDecl = struct {
    // error: no constraints specified
    f0 vector<uint16>:;
    // error: no constraints specified
    f1 vector<uint16>:[];
    // error: leading comma
    f2 vector<uint16>:[,16,optional];
    // error: trailing comma
    f3 vector<uint16>:[16,optional,];
    // error: double comma
    f4 vector<uint16>:[16,,optional];
    // error: missing comma
    f5 vector<uint16>:[16 optional];
    // error: unnecessary brackets
    f6 vector<uint16>:[16];
    // error (x2): unnecessary brackets, missing close bracket
    f7 vector<uint16>:[16;
    // error (x2): invalid constant, missing list brackets
    f8 vector<uint16>:1~6,optional;
    // error (x4): leading/double/trailing comma, missing list brackets
    f9 vector<uint16>:,16,,optional,;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  EXPECT_EQ(errors.size(), 15);
  ASSERT_ERR(errors[0], fidl::ErrEmptyConstraints);
  ASSERT_ERR(errors[1], fidl::ErrEmptyConstraints);
  ASSERT_ERR(errors[2], fidl::ErrLeadingComma);
  ASSERT_ERR(errors[3], fidl::ErrTrailingComma);
  ASSERT_ERR(errors[4], fidl::ErrConsecutiveComma);
  ASSERT_ERR(errors[5], fidl::ErrMissingComma);
  ASSERT_ERR(errors[6], fidl::ErrUnnecessaryConstraintBrackets);
  ASSERT_ERR(errors[7], fidl::ErrUnexpectedTokenOfKind);
  ASSERT_ERR(errors[8], fidl::ErrUnnecessaryConstraintBrackets);
  ASSERT_ERR(errors[9], fidl::ErrInvalidCharacter);
  ASSERT_ERR(errors[10], fidl::ErrMissingConstraintBrackets);
  ASSERT_ERR(errors[11], fidl::ErrLeadingComma);
  ASSERT_ERR(errors[12], fidl::ErrConsecutiveComma);
  ASSERT_ERR(errors[13], fidl::ErrTrailingComma);
  ASSERT_ERR(errors[14], fidl::ErrMissingConstraintBrackets);
}

TEST(NewSyntaxTests, DisallowUsingAlias) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

using foo = uint8;
)FIDL",
                      std::move(experimental_flags));

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  EXPECT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrOldUsingSyntaxDeprecated);
}

// Ensure that we don't accidentally enable the new syntax when the new syntax
// flag is not enabled.
TEST(NewSyntaxTests, TypedChannelNewInOld) {
  {
    TestLibrary library(R"FIDL(
library test;

protocol MyProtocol {};

struct Foo {
  client_end:MyProtocol foo;
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 2);
    ASSERT_ERR(errors[0], fidl::ErrExpectedValueButGotType);
    ASSERT_ERR(errors[1], fidl::ErrCouldNotParseSizeBound);
  }

  {
    TestLibrary library(R"FIDL(
library test;

protocol MyProtocol {};

struct Foo {
  server_end:MyProtocol foo;
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_EQ(errors.size(), 2);
    ASSERT_ERR(errors[0], fidl::ErrExpectedValueButGotType);
    ASSERT_ERR(errors[1], fidl::ErrCouldNotParseSizeBound);
  }
}

// Ensure that we don't accidentally enable the old syntax when the new syntax
// flag is enabled.
TEST(NewSyntaxTests, TypedChannelOldInNew) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library test;

protocol MyProtocol {};

type Foo = struct {
  foo MyProtocol;
};

)FIDL",
                      std::move(experimental_flags));
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotUseProtocol);
}

// The new syntax works when the new syntax flag is enabled.
TEST(NewSyntaxTests, TypedChannelNewInNew) {
  // TODO(fcz): make accompanying typespace change
}

}  // namespace
