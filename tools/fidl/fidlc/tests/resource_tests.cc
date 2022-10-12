// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/lexer.h"
#include "tools/fidl/fidlc/include/fidl/names.h"
#include "tools/fidl/fidlc/include/fidl/parser.h"
#include "tools/fidl/fidlc/include/fidl/source_file.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(ResourceTests, GoodValid) {
  TestLibrary library;
  library.AddFile("good/fi-0029.test.fidl");
  ASSERT_COMPILED(library);

  auto resource = library.LookupResource("SomeResource");
  ASSERT_NOT_NULL(resource);

  ASSERT_EQ(resource->properties.size(), 1u);
  EXPECT_EQ(resource->properties[0].name.data(), "subtype");
  EXPECT_EQ(resource->properties[0].type_ctor->layout.resolved().element(),
            library.LookupEnum("MyEnum"));

  ASSERT_NOT_NULL(resource->subtype_ctor);
  auto underlying = resource->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint32);
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
  EXPECT_EQ(resource->properties[0].name.data(), "subtype");
  EXPECT_EQ(resource->properties[0].type_ctor->layout.resolved().element(),
            library.LookupEnum("MyEnum"));

  ASSERT_NOT_NULL(resource->subtype_ctor);
  auto underlying = resource->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint32);
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
  TestLibrary library;
  library.AddFile("bad/fi-0029.test.fidl");
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
