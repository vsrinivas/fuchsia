// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(ResourceTests, GoodValid) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : uint32 {
  NONE = 0;
};

resource_definition SomeResource : uint32 {
  properties {
    MyEnum subtype;
  };
};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto resource = library.LookupResource("SomeResource");
  ASSERT_NOT_NULL(resource);

  ASSERT_EQ(resource->properties.size(), 1u);
  EXPECT_EQ(fidl::flat::GetName(resource->properties[0].type_ctor).span()->data(), "MyEnum");
  EXPECT_EQ(resource->properties[0].name.data(), "subtype");

  ASSERT_TRUE(fidl::flat::IsTypeConstructorDefined(resource->subtype_ctor));
  EXPECT_EQ(fidl::flat::GetName(resource->subtype_ctor).span()->data(), "uint32");
}

TEST(ResourceTests, BadEmpty) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

resource_definition SomeResource : uint32 {
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedIdentifier);
}

TEST(ResourceTests, BadNoProperties) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

resource_definition SomeResource : uint32 {
  properties {
  };
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveOneProperty);
}

TEST(ResourceTests, BadDuplicateProperty) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyEnum = enum {
  X = 0;
};

resource_definition SomeResource : uint32 {
  properties {
    stuff MyEnum;
    stuff MyEnum;
  };
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateResourcePropertyName);
}

}  // namespace
