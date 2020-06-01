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

TEST(Resource, Valid) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : uint32 {
  NONE = 0;
};

resource SomeResource : uint32 {
  properties {
    MyEnum subtype;
  };
};

)FIDL");
  ASSERT_TRUE(library.Compile());

  auto resource = library.LookupResource("SomeResource");
  ASSERT_NOT_NULL(resource);

  ASSERT_EQ(resource->properties.size(), 1u);
  EXPECT_EQ(resource->properties[0].type_ctor->name.span()->data(), "MyEnum");
  EXPECT_EQ(resource->properties[0].name.data(), "subtype");

  ASSERT_NOT_NULL(resource->subtype_ctor);
  EXPECT_EQ(resource->subtype_ctor->name.span()->data(), "uint32");
}

TEST(Resource, InvalidEmpty) {
  TestLibrary library(R"FIDL(
library example;

resource SomeResource : uint32 {
};

)FIDL");
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1u);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedIdentifier);
}

TEST(Resource, InvalidNoProperties) {
  TestLibrary library(R"FIDL(
library example;

resource SomeResource : uint32 {
  properties {
  };
};

)FIDL");
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1u);
  ASSERT_ERR(errors[0], fidl::ErrMustHaveOneProperty);
}

TEST(Resource, InvalidDuplicateProperty) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum {
  X = 0;
};

resource SomeResource : uint32 {
  properties {
    MyEnum stuff;
    MyEnum stuff;
  };
};

)FIDL");
  ASSERT_FALSE(library.Compile());

  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1u);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateResourcePropertyName);
}

}  // namespace
