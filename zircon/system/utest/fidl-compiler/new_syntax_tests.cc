// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/70186): Remove this file

// NOTE: this file contains unittests for the "read side" of the new syntax.
// Once the read functionality catches up to the write functionality (fidlconv),
// these tests should look to be merged with the converter tests so that each
// test case both produces new syntax and ensures that it compiles and has
// IR and coding tables that match the output from compiling the old syntax.

#include <optional>

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostic_types.h"
#include "fidl/diagnostics.h"
#include "fidl/experimental_flags.h"
#include "fidl/flat_ast.h"
#include "fidl/utils.h"
#include "test_library.h"

namespace {

using fidl::flat::GetLayoutInvocation;
using fidl::flat::GetName;
using fidl::flat::GetType;

TEST(NewSyntaxTests, SyntaxTokenCases) {
  struct Case {
    const std::optional<fidl::ExperimentalFlags::Flag> flag;
    const bool has_token;
    const fidl::utils::Syntax syntax;
    const std::optional<fidl::diagnostics::DiagnosticDef> error;
  };

  // test every combination of flag value (no flag, old only, either, new only),
  // token/no token, old/new syntax.
  std::vector<Case> cases = {
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kOldSyntaxOnly,
          .has_token = false,
          .syntax = fidl::utils::Syntax::kOld,
          .error = std::nullopt,
      },
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kOldSyntaxOnly,
          .has_token = false,
          .syntax = fidl::utils::Syntax::kNew,
          .error = fidl::ErrExpectedDeclaration,
      },
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kOldSyntaxOnly,
          .has_token = true,
          .syntax = fidl::utils::Syntax::kOld,
          .error = std::nullopt,
      },
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kOldSyntaxOnly,
          .has_token = true,
          .syntax = fidl::utils::Syntax::kNew,
          .error = fidl::ErrExpectedDeclaration,
      },
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kAllowNewSyntax,
          .has_token = false,
          .syntax = fidl::utils::Syntax::kOld,
          .error = fidl::ErrExpectedDeclaration,
      },
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kAllowNewSyntax,
          .has_token = false,
          .syntax = fidl::utils::Syntax::kNew,
          .error = std::nullopt,
      },
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kAllowNewSyntax,
          .has_token = true,
          .syntax = fidl::utils::Syntax::kOld,
          .error = std::nullopt,
      },
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kAllowNewSyntax,
          .has_token = true,
          .syntax = fidl::utils::Syntax::kNew,
          .error = fidl::ErrExpectedDeclaration,
      },
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kNewSyntaxOnly,
          .has_token = false,
          .syntax = fidl::utils::Syntax::kOld,
          .error = fidl::ErrExpectedDeclaration,
      },
      Case{.flag = fidl::ExperimentalFlags::Flag::kNewSyntaxOnly,
           .has_token = false,
           .syntax = fidl::utils::Syntax::kNew,
           .error = std::nullopt},
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kNewSyntaxOnly,
          .has_token = true,
          .syntax = fidl::utils::Syntax::kOld,
          .error = fidl::ErrRemoveSyntaxVersion,
      },
      Case{
          .flag = fidl::ExperimentalFlags::Flag::kNewSyntaxOnly,
          .has_token = true,
          .syntax = fidl::utils::Syntax::kNew,
          .error = fidl::ErrRemoveSyntaxVersion,
      },
      Case{
          .flag = std::nullopt,
          .has_token = false,
          .syntax = fidl::utils::Syntax::kOld,
          .error = std::nullopt,
      },
      Case{
          .flag = std::nullopt,
          .has_token = false,
          .syntax = fidl::utils::Syntax::kNew,
          .error = fidl::ErrExpectedDeclaration,
      },
      Case{
          .flag = std::nullopt,
          .has_token = true,
          .syntax = fidl::utils::Syntax::kOld,
          .error = fidl::ErrRemoveSyntaxVersion,
      },
      Case{
          .flag = std::nullopt,
          .has_token = true,
          .syntax = fidl::utils::Syntax::kNew,
          .error = fidl::ErrRemoveSyntaxVersion,
      },
  };
  for (const auto& test_case : cases) {
    fidl::ExperimentalFlags flags;
    if (test_case.flag)
      flags.SetFlag(*test_case.flag);

    std::ostringstream lib;
    if (test_case.has_token)
      lib << "deprecated_syntax;\n";
    lib << "library example;\n\n";

    if (test_case.syntax == fidl::utils::Syntax::kNew) {
      lib << "type S = struct {};\n";
    } else {
      lib << "struct S {};\n";
    }

    TestLibrary library(lib.str(), flags);
    std::unique_ptr<fidl::raw::File> dont_care;
    if (test_case.error.has_value()) {
      ASSERT_ERRORED_DURING_COMPILE(library, *test_case.error);
    } else {
      ASSERT_COMPILED(library);
    }
  }
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
                      experimental_flags);

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
                      experimental_flags);

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
                      experimental_flags);
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
                      experimental_flags);
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupBits("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
  EXPECT_EQ(GetName(type_decl->subtype_ctor).decl_name(), "uint64");
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
                      experimental_flags);

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
                      experimental_flags);
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
                      experimental_flags);
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupEnum("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
  EXPECT_EQ(GetName(type_decl->subtype_ctor).decl_name(), "int32");
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
                      experimental_flags);
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
                      experimental_flags);

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
                      experimental_flags);
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
                               experimental_flags);

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
                               experimental_flags);

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
                      experimental_flags);
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
                               experimental_flags);

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
                      experimental_flags);

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
                               experimental_flags);

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
                      experimental_flags);

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
                      experimental_flags);
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 5);
  auto type_decl_f0 = library.LookupBits("F0");
  ASSERT_NOT_NULL(type_decl_f0);
  EXPECT_EQ(type_decl_f0->members.size(), 1);
  auto type_decl_f1 = library.LookupEnum("F1");
  ASSERT_NOT_NULL(type_decl_f1);
  EXPECT_EQ(type_decl_f1->members.size(), 1);
  auto type_decl_f2 = library.LookupStruct("F2");
  ASSERT_NOT_NULL(type_decl_f2);
  EXPECT_EQ(type_decl_f2->members.size(), 2);
  auto type_decl_f3 = library.LookupTable("F3");
  ASSERT_NOT_NULL(type_decl_f3);
  EXPECT_EQ(type_decl_f3->members.size(), 1);
  auto type_decl_f4 = library.LookupUnion("F4");
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
                      experimental_flags);

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
                      experimental_flags);
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);
  ASSERT_NOT_NULL(library.LookupTypeAlias("AliasOfDecl"));
}

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
       i2 struct{};
       i3 array<struct{},5>;
     },5>;
};
)FIDL",
                      experimental_flags);

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 8);
  auto type_decl_vector_anon = library.LookupStruct("V3");
  ASSERT_NOT_NULL(type_decl_vector_anon);
  EXPECT_EQ(type_decl_vector_anon->members.size(), 2);
  ASSERT_NOT_NULL(library.LookupStruct("I0"));
  ASSERT_NOT_NULL(library.LookupStruct("I1"));
  auto type_decl_array_anon = library.LookupStruct("A3");
  ASSERT_NOT_NULL(type_decl_array_anon);
  EXPECT_EQ(type_decl_array_anon->members.size(), 2);
  ASSERT_NOT_NULL(library.LookupStruct("I2"));
  ASSERT_NOT_NULL(library.LookupStruct("I3"));
}

TEST(NewSyntaxTests, GoodLayoutMemberConstraints) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  // TODO(fxbug.dev/65978): a number of fields in this struct declaration have
  //  been commented out until their respective features (client/server_end)
  //  have been added to the compiler.
  TestLibrary library(R"FIDL(
library example;

alias TypeAlias = vector<uint8>;
type t1 = resource struct {
  u0 union { 1: b bool; };
  u1 union { 1: b bool; }:optional;
};
)FIDL",
                      experimental_flags);
  ASSERT_COMPILED(library);

  auto type_decl = library.LookupStruct("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);

  size_t i = 0;

  auto u0_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(u0_type_base->kind, fidl::flat::Type::Kind::kIdentifier);
  auto u0_type = static_cast<const fidl::flat::IdentifierType*>(u0_type_base);
  EXPECT_EQ(u0_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(u0_type->type_decl->kind, fidl::flat::Decl::Kind::kUnion);

  auto u1_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(u1_type_base->kind, fidl::flat::Type::Kind::kIdentifier);
  auto u1_type = static_cast<const fidl::flat::IdentifierType*>(u1_type_base);
  EXPECT_EQ(u1_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(u1_type->type_decl->kind, fidl::flat::Decl::Kind::kUnion);
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
    // errors[5]: missing comma, errors[6], errors[7]: consume > and ; trying
    // to get to next member
    f5 vector<uint16>:<16 optional>;
    // errors[8] missing close bracket
    f7 vector<uint16>:<16;
    // errors[10]: invalid constant
    f8 vector<uint16>:1~6,optional;
    // errors[11]: unexpected token
    f9 vector<uint16>:,16,,optional,;
};
)FIDL",
                      experimental_flags);

  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 11);
  EXPECT_ERR(errors[0], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[1], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[2], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[3], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[4], fidl::ErrUnexpectedToken);
  EXPECT_ERR(errors[5], fidl::ErrUnexpectedTokenOfKind);
  EXPECT_ERR(errors[6], fidl::ErrUnexpectedTokenOfKind);
  EXPECT_ERR(errors[7], fidl::ErrUnexpectedTokenOfKind);
  EXPECT_ERR(errors[8], fidl::ErrUnexpectedTokenOfKind);
  EXPECT_ERR(errors[9], fidl::ErrInvalidCharacter);
  EXPECT_ERR(errors[10], fidl::ErrUnexpectedToken);
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
                      experimental_flags);
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
                      experimental_flags);

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  ASSERT_EQ(type_decl->members.size(), 16);

  size_t i = 0;

  auto v0_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(v0_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto v0_type = static_cast<const fidl::flat::VectorType*>(v0_type_base);
  EXPECT_EQ(v0_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(v0_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(v0_type->element_count, &fidl::flat::VectorType::kMaxSize);

  auto v1_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(v1_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto v1_type = static_cast<const fidl::flat::VectorType*>(v1_type_base);
  EXPECT_EQ(v1_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(v1_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(v1_type->element_count->value, 16u);

  auto v2_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(v2_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto v2_type = static_cast<const fidl::flat::VectorType*>(v2_type_base);
  EXPECT_EQ(v2_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(v2_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(v2_type->element_count, &fidl::flat::VectorType::kMaxSize);

  auto v3_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(v3_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto v3_type = static_cast<const fidl::flat::VectorType*>(v3_type_base);
  EXPECT_EQ(v3_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(v3_type->element_count->value, 16u);

  auto b4_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(b4_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto b4_type = static_cast<const fidl::flat::VectorType*>(b4_type_base);
  EXPECT_EQ(b4_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(b4_type->element_count, &fidl::flat::VectorType::kMaxSize);

  auto b5_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(b5_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto b5_type = static_cast<const fidl::flat::VectorType*>(b5_type_base);
  EXPECT_EQ(b5_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(b5_type->element_count->value, 16u);

  auto b6_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(b6_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto b6_type = static_cast<const fidl::flat::VectorType*>(b6_type_base);
  EXPECT_EQ(b6_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(b6_type->element_count, &fidl::flat::VectorType::kMaxSize);

  auto b7_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(b7_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto b7_type = static_cast<const fidl::flat::VectorType*>(b7_type_base);
  EXPECT_EQ(b7_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(b7_type->element_count->value, 16u);

  auto s8_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(s8_type_base->kind, fidl::flat::Type::Kind::kString);
  auto s8_type = static_cast<const fidl::flat::StringType*>(s8_type_base);
  EXPECT_EQ(s8_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(s8_type->max_size, &fidl::flat::StringType::kMaxSize);

  auto s9_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(s9_type_base->kind, fidl::flat::Type::Kind::kString);
  auto s9_type = static_cast<const fidl::flat::StringType*>(s9_type_base);
  EXPECT_EQ(s9_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(s9_type->max_size->value, 16u);

  auto s10_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(s10_type_base->kind, fidl::flat::Type::Kind::kString);
  auto s10_type = static_cast<const fidl::flat::StringType*>(s10_type_base);
  EXPECT_EQ(s10_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(s10_type->max_size, &fidl::flat::StringType::kMaxSize);

  auto s11_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(s11_type_base->kind, fidl::flat::Type::Kind::kString);
  auto s11_type = static_cast<const fidl::flat::StringType*>(s11_type_base);
  EXPECT_EQ(s11_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(s11_type->max_size->value, 16u);

  auto a12_invocation = GetLayoutInvocation(type_decl->members[i].type_ctor);
  EXPECT_NULL(a12_invocation.element_type_resolved);
  EXPECT_EQ(a12_invocation.nullability, fidl::types::Nullability::kNonnullable);
  auto a12_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(a12_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto a12_type = static_cast<const fidl::flat::VectorType*>(a12_type_base);
  EXPECT_EQ(a12_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(a12_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(a12_type->element_count, &fidl::flat::VectorType::kMaxSize);
  EXPECT_NULL(a12_invocation.size_resolved);

  auto a13_invocation = GetLayoutInvocation(type_decl->members[i].type_ctor);
  EXPECT_NULL(a13_invocation.element_type_resolved);
  EXPECT_EQ(a13_invocation.nullability, fidl::types::Nullability::kNonnullable);
  auto a13_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(a13_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto a13_type = static_cast<const fidl::flat::VectorType*>(a13_type_base);
  EXPECT_EQ(a13_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(a13_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(a13_type->element_count->value, 16u);
  EXPECT_EQ(a13_type->element_count, a13_invocation.size_resolved);

  auto a14_invocation = GetLayoutInvocation(type_decl->members[i].type_ctor);
  EXPECT_NULL(a14_invocation.element_type_resolved);
  EXPECT_EQ(a14_invocation.nullability, fidl::types::Nullability::kNullable);
  auto a14_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(a14_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto a14_type = static_cast<const fidl::flat::VectorType*>(a14_type_base);
  EXPECT_EQ(a14_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(a14_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(a14_type->element_count, &fidl::flat::VectorType::kMaxSize);
  // EXPECT_EQ(a14_type->element_count, a14_invocation->maybe_size);
  EXPECT_NULL(a14_invocation.size_resolved);

  auto a15_invocation = GetLayoutInvocation(type_decl->members[i].type_ctor);
  EXPECT_NULL(a15_invocation.element_type_resolved);
  EXPECT_EQ(a15_invocation.nullability, fidl::types::Nullability::kNullable);
  auto a15_type_base = GetType(type_decl->members[i++].type_ctor);
  ASSERT_EQ(a15_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto a15_type = static_cast<const fidl::flat::VectorType*>(a15_type_base);
  EXPECT_EQ(a15_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(a15_type->element_count->value, 16u);
  EXPECT_EQ(a15_type->element_count, a15_invocation.size_resolved);
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
                      experimental_flags);

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  ASSERT_EQ(type_decl->members.size(), 6);
  size_t i = 0;

  auto& u0 = type_decl->members[i++];
  auto u0_type = static_cast<const fidl::flat::IdentifierType*>(GetType(u0.type_ctor));
  EXPECT_EQ(u0_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& u1 = type_decl->members[i++];
  auto u1_type = static_cast<const fidl::flat::IdentifierType*>(GetType(u1.type_ctor));
  EXPECT_EQ(u1_type->nullability, fidl::types::Nullability::kNullable);

  auto& u2 = type_decl->members[i++];
  auto u2_type = static_cast<const fidl::flat::IdentifierType*>(GetType(u2.type_ctor));
  EXPECT_EQ(u2_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& u3 = type_decl->members[i++];
  auto u3_type = static_cast<const fidl::flat::IdentifierType*>(GetType(u3.type_ctor));
  EXPECT_EQ(u3_type->nullability, fidl::types::Nullability::kNullable);

  auto& u4 = type_decl->members[i++];
  auto u4_type = static_cast<const fidl::flat::IdentifierType*>(GetType(u4.type_ctor));
  EXPECT_EQ(u4_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& u5 = type_decl->members[i++];
  auto u5_type = static_cast<const fidl::flat::IdentifierType*>(GetType(u5.type_ctor));
  EXPECT_EQ(u5_type->nullability, fidl::types::Nullability::kNullable);
}

TEST(NewSyntaxTests, GoodConstraintsOnHandles) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

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
                               experimental_flags);

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  ASSERT_EQ(type_decl->members.size(), 6);

  auto& h0 = type_decl->members[0];
  auto h0_type = static_cast<const fidl::flat::HandleType*>(GetType(h0.type_ctor));
  EXPECT_EQ(h0_type->obj_type, 0u);
  EXPECT_EQ(h0_type->rights, &fidl::flat::HandleType::kSameRights);
  EXPECT_EQ(h0_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& h1 = type_decl->members[1];
  auto h1_type = static_cast<const fidl::flat::HandleType*>(GetType(h1.type_ctor));
  EXPECT_NE(h1_type->obj_type, 0u);
  EXPECT_EQ(h1_type->rights, &fidl::flat::HandleType::kSameRights);
  EXPECT_EQ(h1_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& h2 = type_decl->members[2];
  auto h2_type = static_cast<const fidl::flat::HandleType*>(GetType(h2.type_ctor));
  EXPECT_EQ(h2_type->obj_type, 0u);
  EXPECT_EQ(h2_type->rights, &fidl::flat::HandleType::kSameRights);
  EXPECT_EQ(h2_type->nullability, fidl::types::Nullability::kNullable);

  auto& h3 = type_decl->members[3];
  auto h3_type = static_cast<const fidl::flat::HandleType*>(GetType(h3.type_ctor));
  EXPECT_EQ(h3_type->obj_type, 3u);  // VMO
  EXPECT_EQ(h3_type->rights, &fidl::flat::HandleType::kSameRights);
  EXPECT_EQ(h3_type->nullability, fidl::types::Nullability::kNullable);

  auto& h4 = type_decl->members[4];
  auto h4_type = static_cast<const fidl::flat::HandleType*>(GetType(h4.type_ctor));
  EXPECT_EQ(h4_type->obj_type, 3u);          // VMO
  EXPECT_EQ(h4_type->rights->value, 0x02u);  // TRANSFER
  EXPECT_EQ(h4_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& h5 = type_decl->members[5];
  auto h5_type = static_cast<const fidl::flat::HandleType*>(GetType(h5.type_ctor));
  EXPECT_EQ(h5_type->obj_type, 3u);          // VMO
  EXPECT_EQ(h5_type->rights->value, 0x02u);  // TRANSFER
  EXPECT_EQ(h5_type->nullability, fidl::types::Nullability::kNullable);
}

// Ensure that we don't accidentally enable the new syntax when the new syntax
// flag is not enabled.
TEST(NewSyntaxTests, BadTypedChannelNewInOld) {
  {
    TestLibrary library(R"FIDL(
library test;

protocol MyProtocol {};

struct Foo {
  client_end:MyProtocol foo;
};

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
  }

  {
    TestLibrary library(R"FIDL(
library test;

protocol MyProtocol {};

struct Foo {
  server_end:MyProtocol foo;
};

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
  }
}

// Ensure that we don't accidentally enable the old syntax when the new syntax
// flag is enabled.
TEST(NewSyntaxTests, BadTypedChannelOldInNew) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  {
    TestLibrary library(R"FIDL(
library test;

protocol MyProtocol {};

type Foo = struct {
  foo MyProtocol;
};

)FIDL",
                        experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotUseProtocol);
  }

  {
    TestLibrary library(R"FIDL(
library test;

protocol MyProtocol {};

type Foo = resource struct {
  foo request<MyProtocol>;
};

)FIDL",
                        experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
  }

  {
    TestLibrary library(R"FIDL(
library test;

type Bar = struct {};

type Foo = resource struct {
  foo request<Bar>;
};

)FIDL",
                        experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
  }
}

// The new syntax works when the new syntax flag is enabled.
TEST(NewSyntaxTests, GoodTypedChannelNewInNew) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library test;

protocol MyProtocol {};

type Foo = resource struct {
  foo client_end:MyProtocol;
  bar server_end:MyProtocol;
  maybe_foo client_end:<MyProtocol, optional>;
  maybe_bar server_end:<MyProtocol, optional>;
};

)FIDL",
                      experimental_flags);
  ASSERT_COMPILED(library);
}

TEST(NewSyntaxTests, BadBoxInOldSyntax) {
  TestLibrary library(R"FIDL(
library test;

struct Foo {};

struct Bar {
  box<Foo> foo;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
}

TEST(NewSyntaxTests, BadTooManyLayoutParameters) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  foo uint8<8>;
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(NewSyntaxTests, BadNotEnoughParameters) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  foo array<8>;
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(NewSyntaxTests, BadTooManyConstraints) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  foo uint8:<1, 2, 3>;
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

TEST(NewSyntaxTests, BadParameterizedAnonymousLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  bar struct {}<1>;
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(NewSyntaxTests, BadConstrainTwice) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

alias MyVmo = zx.handle:VMO;

type Foo = struct {
    foo MyVmo:CHANNEL;
};

)FIDL",
                               experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotConstrainTwice);
}

TEST(NewSyntaxTests, GoodNoOverlappingConstraints) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  auto library = WithLibraryZx(R"FIDL(
library example;

using zx;

alias MyVmo = zx.handle:<VMO, zx.rights.TRANSFER>;

type Foo = resource struct {
    foo MyVmo:optional;
};

)FIDL",
                               experimental_flags);

  ASSERT_COMPILED(library);
}

TEST(NewSyntaxTests, BadWantTypeLayoutParameter) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
    foo vector<3>;
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedType);
}

TEST(NewSyntaxTests, BadWantValueLayoutParameter) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
    foo array<uint8, uint8>;
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedValueButGotType);
}

TEST(NewSyntaxTests, BadShadowedOptional) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

const optional uint8 = 3;

type Foo = resource struct {
    foo vector<uint8>:<10, optional>;
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(NewSyntaxTests, BadWrongConstraintType) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type Foo = resource struct {
    foo vector<uint8>:"hello";
};
)FIDL",
                      experimental_flags);

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrUnexpectedConstraint);
}

TEST(NewSyntaxTests, BadProtocolMethodNamedParameterList) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct{};
protocol MyProtocol {
  MyMethod(S);
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNamedParameterListTypesNotYetSupported);
}

TEST(NewSyntaxTests, BadProtocolMethodBitsLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod(bits {
    FOO = 1;
  });
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidParameterListType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bits");
}

TEST(NewSyntaxTests, BadProtocolMethodEnumLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod(enum {
    FOO = 1;
  });
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidParameterListType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "enum");
}

TEST(NewSyntaxTests, BadProtocolMethodTableLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod(table {
    1: foo bool;
  });
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNotYetSupportedParameterListType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "table");
}

TEST(NewSyntaxTests, BadProtocolMethodUnionLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod(union {
    1: foo bool;
  });
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNotYetSupportedParameterListType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "union");
}

TEST(NewSyntaxTests, BadProtocolMethodEmptyResponseWithError) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod() -> () error uint32;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrResponsesWithErrorsMustNotBeEmpty);
}

// TODO(fxbug.dev/76349): attributes on struct payloads are not supported for
//  the time being.
TEST(NewSyntaxTests, BadAttributesOnPayloadStruct) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod(@attr struct { s string; });
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNotYetSupportedAttributesOnPayloadStructs);
}

// TODO(fxbug.dev/76349): using empty structs as request/response payloads is
//  only supported in the new syntax.  Until this is supported, we throw a user
//  facing error instead.
TEST(NewSyntaxTests, BadProtocolMethodEmptyRequestStruct) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod(struct {}) -> ();
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrEmptyPayloadStructs);
}

// TODO(fxbug.dev/76349): using empty structs as request/response payloads is
//  only supported in the new syntax.  Until this is supported, we throw a user
//  facing error instead.
TEST(NewSyntaxTests, BadProtocolMethodEmptyResponseStruct) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod() -> (struct {});
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrEmptyPayloadStructs);
}

TEST(NewSyntaxTests, GoodProtocolMethodEmptyStructsWithError) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod() -> (struct {}) error uint32;
};
)FIDL",
                      experimental_flags);
  ASSERT_COMPILED(library);
  auto protocol = library.LookupProtocol("MyProtocol");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);

  auto& method = protocol->methods[0];
  EXPECT_TRUE(method.has_request);
  EXPECT_NULL(method.maybe_request_payload);
  ASSERT_TRUE(method.has_response && method.maybe_response_payload);

  auto response = method.maybe_response_payload;
  EXPECT_TRUE(response->kind == fidl::flat::Decl::Kind::kStruct);
  ASSERT_EQ(response->members.size(), 1);
}

}  // namespace
