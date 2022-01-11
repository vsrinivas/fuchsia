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
  TestLibrary library(R"FIDL(library example;

type MyEnum = strict enum : uint32 {
    NONE = 0;
};

resource_definition SomeResource : uint32 {
    properties {
        subtype MyEnum;
    };
};
)FIDL");
  ASSERT_COMPILED(library);

  auto resource = library.LookupResource("SomeResource");
  ASSERT_NOT_NULL(resource);

  ASSERT_EQ(resource->properties.size(), 1u);
  EXPECT_EQ(resource->properties[0].type_ctor->name.span()->data(), "MyEnum");
  EXPECT_EQ(resource->properties[0].name.data(), "subtype");

  ASSERT_NOT_NULL(resource->subtype_ctor);
  EXPECT_EQ(resource->subtype_ctor->name.span()->data(), "uint32");
}

TEST(ResourceTests, GoodAliasedBaseType) {
  TestLibrary library(R"FIDL(library example;

type MyEnum = strict enum : uint32 {
    NONE = 0;
};

alias via = uint32;

resource_definition SomeResource : via {
    properties {
        subtype MyEnum;
    };
};
)FIDL");
  ASSERT_COMPILED(library);

  auto resource = library.LookupResource("SomeResource");
  ASSERT_NOT_NULL(resource);

  ASSERT_EQ(resource->properties.size(), 1u);
  EXPECT_EQ(resource->properties[0].type_ctor->name.span()->data(), "MyEnum");
  EXPECT_EQ(resource->properties[0].name.data(), "subtype");

  ASSERT_NOT_NULL(resource->subtype_ctor);
  ASSERT_NOT_NULL(resource->subtype_ctor->type);
  ASSERT_EQ(resource->subtype_ctor->type->kind, fidl::flat::Type::Kind::kPrimitive);
  auto primitive_type = static_cast<const fidl::flat::PrimitiveType*>(resource->subtype_ctor->type);
  EXPECT_EQ(primitive_type->subtype, fidl::types::PrimitiveSubtype::kUint32);
}

TEST(ResourceTests, BadEmpty) {
  TestLibrary library(R"FIDL(
library example;

resource_definition SomeResource : uint32 {
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedIdentifier);
}

TEST(ResourceTests, BadNoProperties) {
  TestLibrary library(R"FIDL(
library example;

resource_definition SomeResource : uint32 {
  properties {
  };
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveOneProperty);
}

TEST(ResourceTests, BadDuplicateProperty) {
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

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateResourcePropertyName);
}

TEST(ResourceTests, BadNotUint32) {
  TestLibrary library(R"FIDL(library example;

type MyEnum = strict enum : uint32 {
    NONE = 0;
};

resource_definition SomeResource : uint8 {
    properties {
        subtype MyEnum;
    };
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrResourceMustBeUint32Derived);
}

}  // namespace
