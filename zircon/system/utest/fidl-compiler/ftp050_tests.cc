// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(Ftp050Tests, TypeDeclOfStructLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kFtp050);
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
TEST(Ftp050Tests, TypeDeclOfUnionLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kFtp050);
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

TEST(Ftp050Tests, TypeDeclOfStructLayoutWithAnonymousStruct) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kFtp050);
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

}  // namespace
