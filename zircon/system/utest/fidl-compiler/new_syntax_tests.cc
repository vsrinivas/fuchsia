// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

// Tests that use handles need to define a "fake" zx dependency, written in the
// old syntax. This helper function streamlines that process. It also serves as
// a good psuedo-test for situations where a library written in the new syntax
// depends on one written in the old.
TestLibrary with_fake_zx(SharedAmongstLibraries& shared, const std::string& in,
                         const fidl::ExperimentalFlags flags) {
  TestLibrary main_lib("example.fidl", in, &shared, flags);
  fidl::ExperimentalFlags zx_flags;
  zx_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  std::string zx = R"FIDL(
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

  auto library = with_fake_zx(shared, R"FIDL(
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

TEST(NewSyntaxTests, TypeDeclOfUnionLayoutWithResourceness) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;

  auto library = with_fake_zx(shared, R"FIDL(
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

  auto library = with_fake_zx(shared, R"FIDL(
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
      data array<uint8>:16;
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

}  // namespace
