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

TEST(NewSyntaxTests, GoodSyntaxVersionOmitted) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type S = struct{};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED(library);
}

TEST(NewSyntaxTests, BadSyntaxVersionOmittedMismatch) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
deprecated_syntax;
library example;

type S = struct{};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(NewSyntaxTests, GoodSyntaxVersionDeprecated) {
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

TEST(NewSyntaxTests, BadSyntaxVersionDeprecatedMismatch) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
deprecated_syntax;
library example;

type S = struct{};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(NewSyntaxTests, BadSyntaxVersionMismatch) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

struct S {};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(NewSyntaxTests, BadSyntaxVersionWithoutFlag) {
  TestLibrary library(R"FIDL(
deprecated_syntax;
library example;
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrRemoveSyntaxVersion);
}

TEST(NewSyntaxTests, BadSyntaxVersionMisplaced) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
deprecated_syntax;
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMisplacedSyntaxVersion);
}

TEST(NewSyntaxTests, BadSyntaxVersionMisplacedWithoutFlag) {
  TestLibrary library(R"FIDL(
library example;
deprecated_syntax;
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrRemoveSyntaxVersion);
}

TEST(NewSyntaxTests, BadSyntaxVersionRepeated) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
deprecated_syntax;
library example;
deprecated_syntax;
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMisplacedSyntaxVersion);
}

TEST(NewSyntaxTests, GoodTypeDeclOfBitsLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = bits {
    FOO = 1;
    BAR = 2;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupBits("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
}

TEST(NewSyntaxTests, GoodTypeDeclOfBitsLayoutWithSubtype) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = bits : uint64 {
    FOO = 1;
    BAR = 2;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupBits("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
  EXPECT_EQ(type_decl->subtype_ctor->name.decl_name(), "uint64");
}

TEST(NewSyntaxTests, GoodTypeDeclOfBitsLayoutWithStrictnesss) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;
type t1 = bits {
    FOO = 1;
};
type t2 = flexible bits {
    FOO = 1;
};
type t3 = strict bits {
    FOO = 1;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED(library);

  auto type_decl = library.LookupBits("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);

  type_decl = library.LookupBits("t2");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);

  type_decl = library.LookupBits("t3");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kStrict);
}

TEST(NewSyntaxTests, GoodTypeDeclOfEnumLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = enum {
    FOO = 1;
    BAR = 2;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupEnum("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
}

TEST(NewSyntaxTests, GoodTypeDeclOfEnumLayoutWithSubtype) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = enum : int32 {
    FOO = 1;
    BAR = 2;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupEnum("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
  EXPECT_EQ(type_decl->subtype_ctor->name.decl_name(), "int32");
}

TEST(NewSyntaxTests, BadTypeDeclOfEnumLayoutWithInvalidSubtype) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = enum : "123" {
    FOO = 1;
    BAR = 2;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidWrappedType);
}

TEST(NewSyntaxTests, GoodTypeDeclOfEnumLayoutWithStrictnesss) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;
type t1 = enum {
    FOO = 1;
};
type t2 = flexible enum {
    FOO = 1;
};
type t3 = strict enum {
    FOO = 1;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED(library);

  auto type_decl = library.LookupEnum("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);

  type_decl = library.LookupEnum("t2");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kFlexible);

  type_decl = library.LookupEnum("t3");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->strictness, fidl::types::Strictness::kStrict);
}

TEST(NewSyntaxTests, GoodTypeDeclOfStructLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = struct {
    field1 uint16 = 5;
    field2 uint16;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
}

TEST(NewSyntaxTests, GoodTypeDeclOfStructLayoutWithResourceness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  auto library = WithLibraryZx(R"FIDL(
library example;
using zx;
type t1 = struct {
    f1 uint8;
};
type t2 = resource struct {
    f1 zx.handle;
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_COMPILED(library);

  auto type_decl = library.LookupStruct("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kValue);

  type_decl = library.LookupStruct("t2");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kResource);
}

TEST(NewSyntaxTests, GoodTypeDeclOfTableLayoutWithResourceness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  auto library = WithLibraryZx(R"FIDL(
library example;
using zx;
type t1 = table {
    1: f1 uint8;
};
type t2 = resource table {
    1: f1 zx.handle;
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_COMPILED(library);

  auto type_decl = library.LookupTable("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kValue);

  type_decl = library.LookupTable("t2");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kResource);
}

TEST(NewSyntaxTests, GoodTypeDeclOfUnionLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = union {
    1: variant1 uint16;
    2: reserved;
    3: variant2 uint16;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupUnion("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 3);
}

TEST(NewSyntaxTests, GoodTypeDeclOfUnionLayoutWithResourceness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  auto library = WithLibraryZx(R"FIDL(
library example;
using zx;
type t1 = union {
    1: v1 uint8;
};
type t2 = resource union {
    1: v1 zx.handle;
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
  EXPECT_EQ(type_decl->resourceness, fidl::types::Resourceness::kResource);
}

TEST(NewSyntaxTests, GoodTypeDeclOfUnionLayoutWithStrictnesss) {
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

TEST(NewSyntaxTests, GoodTypeDeclOfUnionLayoutWithResourcenessAndStrictness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  auto library = WithLibraryZx(R"FIDL(
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
                               std::move(experimental_flags));

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

TEST(NewSyntaxTests, BadTypeDeclDisallowPartialModifiers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type t1 = union { 1: foo uint8; };
type t2 = strict t1;
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotSpecifyModifier);
}

TEST(NewSyntaxTests, GoodTypeDeclOfAnonymousLayouts) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = struct {
    f0 bits {
      FOO = 1;
    };
    f1 enum {
      BAR = 1;
    };
    f2 struct {
      i0 vector<uint8>;
      i1 string = "foo";
    };
    f3 table {
      1: i0 bool;
    };
    f4 union {
      1: i0 bool;
    };
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 5);
  auto type_decl_f0 = library.LookupBits("TypeDeclF0");
  ASSERT_NOT_NULL(type_decl_f0);
  EXPECT_EQ(type_decl_f0->members.size(), 1);
  auto type_decl_f1 = library.LookupEnum("TypeDeclF1");
  ASSERT_NOT_NULL(type_decl_f1);
  EXPECT_EQ(type_decl_f1->members.size(), 1);
  auto type_decl_f2 = library.LookupStruct("TypeDeclF2");
  ASSERT_NOT_NULL(type_decl_f2);
  EXPECT_EQ(type_decl_f2->members.size(), 2);
  auto type_decl_f3 = library.LookupTable("TypeDeclF3");
  ASSERT_NOT_NULL(type_decl_f3);
  EXPECT_EQ(type_decl_f3->members.size(), 1);
  auto type_decl_f4 = library.LookupUnion("TypeDeclF4");
  ASSERT_NOT_NULL(type_decl_f4);
  EXPECT_EQ(type_decl_f4->members.size(), 1);
}

TEST(NewSyntaxTests, BadTypeDeclOfNewTypeErrors) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type S = struct{};
type N = S;
)FIDL",
                      std::move(experimental_flags));

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNewTypesNotAllowed);
}

TEST(NewSyntaxTests, GoodAlias) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = struct {
    field1 uint16;
    field2 uint16;
};
alias AliasOfDecl = TypeDecl;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
  ASSERT_NOT_NULL(library.LookupTypeAlias("AliasOfDecl"));
}

// TODO(fxbug.dev/71536): add box when its node is added to the flat AST
TEST(NewSyntaxTests, GoodTypeParameters) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;
type Inner = struct{};
alias Alias = Inner;

type TypeDecl = struct {
  // vector of primitive
  v0 vector<uint8>;
  // vector of sourced
  v1 vector<Inner>;
  // vector of alias
  v2 vector<Alias>;
  // vector of anonymous layout
  v3 vector<struct{
       i0 struct{};
       i1 vector<struct{}>;
     }>;
  // array of primitive
  a0 array<uint8,5>;
  // array of sourced
  a1 array<Inner,5>;
  // array of alias
  a2 array<Alias,5>;
  // array of anonymous layout
  a3 array<struct{
       i0 struct{};
       i1 array<struct{},5>;
     },5>;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 8);
  auto type_decl_vector_anon = library.LookupStruct("TypeDeclV3");
  ASSERT_NOT_NULL(type_decl_vector_anon);
  EXPECT_EQ(type_decl_vector_anon->members.size(), 2);
  ASSERT_NOT_NULL(library.LookupStruct("TypeDeclV3I0"));
  ASSERT_NOT_NULL(library.LookupStruct("TypeDeclV3I1"));
  auto type_decl_array_anon = library.LookupStruct("TypeDeclA3");
  ASSERT_NOT_NULL(type_decl_array_anon);
  EXPECT_EQ(type_decl_array_anon->members.size(), 2);
  ASSERT_NOT_NULL(library.LookupStruct("TypeDeclA3I0"));
  ASSERT_NOT_NULL(library.LookupStruct("TypeDeclA3I1"));
}

TEST(NewSyntaxTests, GoodLayoutMemberConstraints) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  // TODO(fxbug.dev/65978): a number of fields in this struct declaration have
  //  been commented out until their respective features (client/server_end)
  //  have been added to the compiler.
  auto library = WithLibraryZx(R"FIDL(
library example;
using zx;
type t1 = resource struct {
  h0 zx.handle;
  h1 zx.handle:optional;
  h2 zx.handle:VMO;
  h3 zx.handle:<VMO,optional>;
  h4 zx.handle:<VMO,zx.rights.DUPLICATE>;
  h5 zx.handle:<VMO,zx.rights.DUPLICATE,optional>;
  u7 union { 1: b bool; };
  u8 union { 1: b bool; }:optional;
  v9 vector<bool>;
  v10 vector<bool>:optional;
  v11 vector<bool>:16;
  v12 vector<bool>:<16,optional>;
  //p13 client_end:MyProtocol;
  //p14 client_end:<MyProtocol,optional>;
  //r15 server_end:P;
  //r16 server_end:<MyProtocol,optional>;
};
)FIDL",
                               std::move(experimental_flags));
  ASSERT_COMPILED(library);

  auto type_decl = library.LookupStruct("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 12);
  // TODO(fxbug.dev/65978): check that the flat AST has proper representation of
  //  each member's constraints. This is blocked on implementing compilation of
  //  the new constraints in the flat AST.
}

// This test ensures that recoverable parsing works as intended for constraints,
// and returns useful and actionable information back to users.
TEST(NewSyntaxTests, BadConstraintsRecoverability) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;
type TypeDecl = struct {
    // errors[0]: no constraints specified
    f0 vector<uint16>:;
    // errors[1]: no constraints specified
    f1 vector<uint16>:<>;
    // errors[2]: leading comma
    f2 vector<uint16>:<,16,optional>;
    // errors[3]: trailing comma
    f3 vector<uint16>:<16,optional,>;
    // errors[4]: double comma
    f4 vector<uint16>:<16,,optional>;
    // errors[5]: missing comma, errors[6]: unecessary brackets
    f5 vector<uint16>:<16 optional>;
    // errors[7]: unnecessary brackets
    f6 vector<uint16>:<16>;
    // errors[8] missing close bracket, errors[9] unnecessary brackets
    f7 vector<uint16>:<16;
    // errors[10]: invalid constant
    f8 vector<uint16>:1~6,optional;
    // errors[11]: unexpected token
    f9 vector<uint16>:,16,,optional,;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 12);
  EXPECT_ERR(errors[0], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[1], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[2], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[3], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[4], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[5], fidl::ErrUnexpectedTokenOfKind);
  EXPECT_ERR(errors[6], fidl::ErrUnnecessaryConstraintBrackets);
  EXPECT_ERR(errors[7], fidl::ErrUnnecessaryConstraintBrackets);
  EXPECT_ERR(errors[8], fidl::ErrUnexpectedTokenOfKind);
  EXPECT_ERR(errors[9], fidl::ErrUnnecessaryConstraintBrackets);
  EXPECT_ERR(errors[10], fidl::ErrInvalidCharacter);
  EXPECT_ERR(errors[11], fidl::ErrUnexpectedToken);
}

// TODO(fxbug.dev/72671): this should be covered by an existing old syntax test
TEST(NewSyntaxTests, GoodConstParsing) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

const MY_NUMBER uint32 = 11259375;
const MY_STRING string:10 = "ten";
const MY_VAR uint32 = MY_NUMBER;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED(library);

  {
    auto decl = library.LookupConstant("MY_NUMBER");
    ASSERT_NOT_NULL(decl);
    ASSERT_EQ(decl->value->kind, fidl::flat::Constant::Kind::kLiteral);
    ASSERT_EQ(decl->value->Value().kind, fidl::flat::ConstantValue::Kind::kUint32);
    auto val = static_cast<const fidl::flat::NumericConstantValue<uint32_t>&>(decl->value->Value());
    EXPECT_EQ(11259375, static_cast<uint32_t>(val));
  }

  {
    auto decl = library.LookupConstant("MY_STRING");
    ASSERT_NOT_NULL(decl);
    ASSERT_EQ(decl->value->kind, fidl::flat::Constant::Kind::kLiteral);
    ASSERT_EQ(decl->value->Value().kind, fidl::flat::ConstantValue::Kind::kString);
    auto val = static_cast<const fidl::flat::StringConstantValue&>(decl->value->Value());
    std::cout << val.value << std::endl;
    EXPECT_EQ(val.value, "\"ten\"");
  }

  {
    auto decl = library.LookupConstant("MY_VAR");
    ASSERT_NOT_NULL(decl);
    ASSERT_EQ(decl->value->kind, fidl::flat::Constant::Kind::kIdentifier);
    ASSERT_EQ(decl->value->Value().kind, fidl::flat::ConstantValue::Kind::kUint32);
    auto val = static_cast<const fidl::flat::NumericConstantValue<uint32_t>&>(decl->value->Value());
    EXPECT_EQ(11259375, static_cast<uint32_t>(val));
  }
}

TEST(NewSyntaxTests, GoodConstraintsOnVectors) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

alias TypeAlias = vector<uint8>;
type TypeDecl= struct {
  v0 vector<bool>;
  v1 vector<bool>:16;
  v2 vector<bool>:optional;
  v3 vector<bool>:<16,optional>;
  b4 bytes;
  b5 bytes:16;
  b6 bytes:optional;
  b7 bytes:<16,optional>;
  s8 string;
  s9 string:16;
  s10 string:optional;
  s11 string:<16,optional>;
  a12 TypeAlias;
  a13 TypeAlias:16;
  a14 TypeAlias:optional;
  a15 TypeAlias:<16,optional>;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  ASSERT_EQ(type_decl->members.size(), 16);

  auto& v0 = type_decl->members[0];
  ASSERT_NULL(v0.type_ctor->maybe_size);
  EXPECT_EQ(v0.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& v1 = type_decl->members[1];
  ASSERT_NOT_NULL(v1.type_ctor->maybe_size);
  EXPECT_EQ(v1.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& v2 = type_decl->members[2];
  ASSERT_NULL(v2.type_ctor->maybe_size);
  EXPECT_EQ(v2.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& v3 = type_decl->members[3];
  ASSERT_NOT_NULL(v3.type_ctor->maybe_size);
  EXPECT_EQ(v3.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& b4 = type_decl->members[4];
  ASSERT_NULL(b4.type_ctor->maybe_size);
  EXPECT_EQ(b4.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& b5 = type_decl->members[5];
  ASSERT_NOT_NULL(b5.type_ctor->maybe_size);
  EXPECT_EQ(b5.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& b6 = type_decl->members[6];
  ASSERT_NULL(b6.type_ctor->maybe_size);
  EXPECT_EQ(b6.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& b7 = type_decl->members[7];
  ASSERT_NOT_NULL(b7.type_ctor->maybe_size);
  EXPECT_EQ(b7.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& s8 = type_decl->members[8];
  ASSERT_NULL(s8.type_ctor->maybe_size);
  EXPECT_EQ(s8.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& s9 = type_decl->members[9];
  ASSERT_NOT_NULL(s9.type_ctor->maybe_size);
  EXPECT_EQ(s9.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& s10 = type_decl->members[10];
  ASSERT_NULL(s10.type_ctor->maybe_size);
  EXPECT_EQ(s10.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& s11 = type_decl->members[11];
  ASSERT_NOT_NULL(s11.type_ctor->maybe_size);
  EXPECT_EQ(s11.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& a12 = type_decl->members[12];
  ASSERT_NULL(a12.type_ctor->maybe_size);
  EXPECT_EQ(a12.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& a13 = type_decl->members[13];
  ASSERT_NOT_NULL(a13.type_ctor->maybe_size);
  EXPECT_EQ(a13.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& a14 = type_decl->members[14];
  ASSERT_NULL(a14.type_ctor->maybe_size);
  EXPECT_EQ(a14.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& a11 = type_decl->members[11];
  ASSERT_NOT_NULL(a11.type_ctor->maybe_size);
  EXPECT_EQ(a11.type_ctor->nullability, fidl::types::Nullability::kNullable);
}

TEST(NewSyntaxTests, GoodConstraintsOnUnions) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type UnionDecl = union{1: foo bool;};
alias UnionAlias = UnionDecl;
type TypeDecl= struct {
  u0 union{1: bar bool;};
  u1 union{1: baz bool;}:optional;
  u2 UnionDecl;
  u3 UnionDecl:optional;
  u4 UnionAlias;
  u5 UnionAlias:optional;
};
)FIDL",
                      std::move(experimental_flags));

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  ASSERT_EQ(type_decl->members.size(), 6);

  auto& u0 = type_decl->members[0];
  EXPECT_EQ(u0.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& u1 = type_decl->members[1];
  EXPECT_EQ(u1.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& u2 = type_decl->members[2];
  EXPECT_EQ(u2.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& u3 = type_decl->members[3];
  EXPECT_EQ(u3.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& u4 = type_decl->members[4];
  EXPECT_EQ(u4.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& u5 = type_decl->members[5];
  EXPECT_EQ(u5.type_ctor->nullability, fidl::types::Nullability::kNullable);
}

TEST(NewSyntaxTests, GoodConstraintsOnHandles) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  auto library = WithLibraryZx(R"FIDL(
library example;
using zx;

type TypeDecl = resource struct {
  h0 zx.handle;
  h1 zx.handle:VMO;
  h2 zx.handle:optional;
  h3 zx.handle:<VMO,optional>;
  h4 zx.handle:<VMO,zx.rights.TRANSFER>;
  h5 zx.handle:<VMO,zx.rights.TRANSFER,optional>;
};
)FIDL",
                               std::move(experimental_flags));

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  ASSERT_EQ(type_decl->members.size(), 6);

  auto& h0 = type_decl->members[0];
  ASSERT_EQ(h0.type_ctor->handle_subtype_identifier, std::nullopt);
  ASSERT_NULL(h0.type_ctor->handle_rights);
  EXPECT_EQ(h0.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& h1 = type_decl->members[1];
  ASSERT_NE(h1.type_ctor->handle_subtype_identifier, std::nullopt);
  ASSERT_NULL(h1.type_ctor->handle_rights);
  EXPECT_EQ(h1.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& h2 = type_decl->members[2];
  ASSERT_EQ(h2.type_ctor->handle_subtype_identifier, std::nullopt);
  ASSERT_NULL(h2.type_ctor->handle_rights);
  EXPECT_EQ(h2.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& h3 = type_decl->members[3];
  ASSERT_NE(h3.type_ctor->handle_subtype_identifier, std::nullopt);
  ASSERT_NULL(h3.type_ctor->handle_rights);
  EXPECT_EQ(h3.type_ctor->nullability, fidl::types::Nullability::kNullable);

  auto& h4 = type_decl->members[4];
  ASSERT_NE(h4.type_ctor->handle_subtype_identifier, std::nullopt);
  ASSERT_NOT_NULL(h4.type_ctor->handle_rights);
  EXPECT_EQ(h4.type_ctor->nullability, fidl::types::Nullability::kNonnullable);

  auto& h5 = type_decl->members[5];
  ASSERT_NE(h5.type_ctor->handle_subtype_identifier, std::nullopt);
  ASSERT_NOT_NULL(h5.type_ctor->handle_rights);
  EXPECT_EQ(h5.type_ctor->nullability, fidl::types::Nullability::kNullable);
}

// TODO(fxbug.dev/71536): once the new flat AST is in, we should add a test for
//  partial constraints being respected.
// TODO(fxbug.dev/68667): Add tests for constraint errors.
// Ensure that we don't accidentally enable the new syntax when the new syntax
// flag is not enabled.
TEST(NewSyntaxTests, GoodTypedChannelNewInOld) {
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
TEST(NewSyntaxTests, GoodTypedChannelOldInNew) {
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
TEST(NewSyntaxTests, GoodTypedChannelNewInNew) {
  // TODO(fcz): make accompanying typespace change
}

}  // namespace
