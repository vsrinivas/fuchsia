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

TEST(ResourceTests, GoodValidWithoutRights) {
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

  ASSERT_NOT_NULL(resource->subtype_ctor);
  auto underlying = resource->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint32);

  auto& subtype = resource->properties[0];
  EXPECT_EQ(subtype.name.data(), "subtype");
  EXPECT_EQ(resource->properties[0].type_ctor->layout.resolved().element(),
            library.LookupEnum("MyEnum"));
}

TEST(ResourceTests, GoodValidWithRights) {
  TestLibrary library(R"FIDL(library example;

type MyEnum = strict enum : uint32 {
    NONE = 0;
};

resource_definition SomeResource : uint32 {
    properties {
        subtype MyEnum;
        rights uint32;
    };
};
)FIDL");
  ASSERT_COMPILED(library);

  auto resource = library.LookupResource("SomeResource");
  ASSERT_NOT_NULL(resource);
  ASSERT_EQ(resource->properties.size(), 2u);

  ASSERT_NOT_NULL(resource->subtype_ctor);
  auto underlying = resource->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint32);

  auto& subtype = resource->properties[0];
  EXPECT_EQ(subtype.name.data(), "subtype");
  EXPECT_EQ(resource->properties[0].type_ctor->layout.resolved().element(),
            library.LookupEnum("MyEnum"));

  auto& rights = resource->properties[1];
  EXPECT_EQ(rights.name.data(), "rights");
  EXPECT_EQ(rights.type_ctor->type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(static_cast<const fidl::flat::PrimitiveType*>(rights.type_ctor->type)->subtype,
            fidl::types::PrimitiveSubtype::kUint32);
}

TEST(ResourceTests, GoodAliasedBaseTypeWithoutRights) {
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

  ASSERT_NOT_NULL(resource->subtype_ctor);
  auto underlying = resource->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint32);

  auto& subtype = resource->properties[0];
  EXPECT_EQ(subtype.name.data(), "subtype");
  EXPECT_EQ(resource->properties[0].type_ctor->layout.resolved().element(),
            library.LookupEnum("MyEnum"));
}

TEST(ResourceTests, GoodAliasedBaseTypeWithRights) {
  TestLibrary library(R"FIDL(library example;

type MyEnum = strict enum : uint32 {
    NONE = 0;
};

alias via = uint32;

resource_definition SomeResource : via {
    properties {
        subtype MyEnum;
        rights via;
    };
};
)FIDL");
  ASSERT_COMPILED(library);

  auto resource = library.LookupResource("SomeResource");
  ASSERT_NOT_NULL(resource);
  ASSERT_EQ(resource->properties.size(), 2u);

  ASSERT_NOT_NULL(resource->subtype_ctor);
  auto underlying = resource->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint32);

  auto& subtype = resource->properties[0];
  EXPECT_EQ(subtype.name.data(), "subtype");
  EXPECT_EQ(resource->properties[0].type_ctor->layout.resolved().element(),
            library.LookupEnum("MyEnum"));

  auto& rights = resource->properties[1];
  EXPECT_EQ(rights.name.data(), "rights");
  EXPECT_EQ(rights.type_ctor->type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(static_cast<const fidl::flat::PrimitiveType*>(rights.type_ctor->type)->subtype,
            fidl::types::PrimitiveSubtype::kUint32);
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
  TestLibrary library;
  library.AddFile("bad/fi-0108.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateResourcePropertyName);
}

TEST(ResourceTests, BadNotUint32) {
  TestLibrary library;
  library.AddFile("bad/fi-0172.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrResourceMustBeUint32Derived);
}

TEST(ResourceTests, BadMissingSubtypePropertyTest) {
  TestLibrary library;
  library.AddFile("bad/fi-0173.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrResourceMissingSubtypeProperty);
}

TEST(ResourceTests, BadSubtypeNotEnum) {
  TestLibrary library;
  library.AddFile("bad/fi-0175.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrResourceSubtypePropertyMustReferToEnum);
}

TEST(ResourceTests, BadSubtypeNotIdentifier) {
  TestLibrary library(R"FIDL(
library example;

resource_definition handle : uint32 {
    properties {
        subtype uint32;
    };
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrResourceSubtypePropertyMustReferToEnum);
}

TEST(ResourceTests, BadNonBitsRights) {
  TestLibrary library;
  library.AddFile("bad/fi-0177.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrResourceRightsPropertyMustReferToBits);
}

TEST(ResourceTests, BadIncludeCycle) {
  TestLibrary library(R"FIDL(
library example;

resource_definition handle : uint32 {
    properties {
        subtype handle;
    };
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrIncludeCycle,
                                      fidl::ErrResourceSubtypePropertyMustReferToEnum);
}

}  // namespace
