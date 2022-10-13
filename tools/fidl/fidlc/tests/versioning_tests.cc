// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/experimental_flags.h"
#include "tools/fidl/fidlc/include/fidl/utils.h"
#include "tools/fidl/fidlc/include/fidl/versioning_types.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

// This file tests the behavior of the @available attribute. See also
// decomposition_tests.cc and availability_interleaving_tests.cc.

namespace {

// Largest numeric version accepted by fidl::Version::Parse.
const std::string kMaxNumericVersion = std::to_string((1ull << 63) - 1);

TEST(VersioningTests, GoodImplicitPlatformOneComponent) {
  TestLibrary library(R"FIDL(
library example;
)FIDL");
  ASSERT_COMPILED(library);

  auto example = library.LookupLibrary("example");
  EXPECT_EQ(example->platform, fidl::Platform::Parse("example").value());
}

TEST(VersioningTests, GoodImplicitPlatformTwoComponents) {
  TestLibrary library(R"FIDL(
library example.something;
)FIDL");
  ASSERT_COMPILED(library);

  auto example = library.LookupLibrary("example.something");
  EXPECT_EQ(example->platform, fidl::Platform::Parse("example").value());
}

TEST(VersioningTests, GoodExplicitPlatform) {
  TestLibrary library(R"FIDL(
@available(platform="someplatform", added=HEAD)
library example;
)FIDL");
  ASSERT_COMPILED(library);

  auto example = library.LookupLibrary("example");
  EXPECT_EQ(example->platform, fidl::Platform::Parse("someplatform").value());
}

TEST(VersioningTests, BadInvalidPlatform) {
  TestLibrary library(R"FIDL(
@available(platform="spaces not allowed", added=HEAD)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidPlatform);
}

TEST(VersioningTests, BadAttributeOnMultipleLibraryDeclarationsAgree) {
  TestLibrary library;
  library.AddSource("first.fidl", R"FIDL(
@available(added=1)
library example;
)FIDL");
  library.AddSource("second.fidl", R"FIDL(
@available(added=1)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttribute);
}

TEST(VersioningTests, BadAttributeOnMultipleLibraryDeclarationsDisagree) {
  TestLibrary library;
  library.AddSource("first.fidl", R"FIDL(
@available(added=1)
library example;
)FIDL");
  library.AddSource("second.fidl", R"FIDL(
@available(added=2)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttribute);
}

TEST(VersioningTests, BadAttributeOnMultipleLibraryDeclarationsHead) {
  TestLibrary library;
  library.AddSource("first.fidl", R"FIDL(
@available(added=HEAD)
library example;
)FIDL");
  library.AddSource("second.fidl", R"FIDL(
@available(added=HEAD)
library example;
)FIDL");
  // TODO(fxbug.dev/111624): Check for duplicate attributes earlier in
  // compilation so that this is ErrDuplicateAttribute instead.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrReferenceInLibraryAttribute);
}

TEST(VersioningTests, GoodLibraryDefault) {
  auto source = R"FIDL(
library example;
)FIDL";

  for (auto version : {"1", "2", kMaxNumericVersion.c_str(), "HEAD", "LEGACY"}) {
    TestLibrary library(source);
    library.SelectVersion("example", version);
    ASSERT_COMPILED(library);
  }
}

TEST(VersioningTests, GoodLibraryAddedAtHead) {
  auto source = R"FIDL(
@available(added=HEAD)
library example;
)FIDL";

  for (auto version : {"1", "2", kMaxNumericVersion.c_str(), "HEAD", "LEGACY"}) {
    TestLibrary library(source);
    library.SelectVersion("example", version);
    ASSERT_COMPILED(library);
  }
}

TEST(VersioningTests, GoodLibraryAddedAtOne) {
  auto source = R"FIDL(
@available(added=1)
library example;
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
  }
}

TEST(VersioningTests, GoodLibraryAddedAndRemoved) {
  auto source = R"FIDL(
@available(added=1, removed=2)
library example;
)FIDL";

  for (auto version : {"1", "2", kMaxNumericVersion.c_str(), "HEAD", "LEGACY"}) {
    TestLibrary library(source);
    library.SelectVersion("example", version);
    ASSERT_COMPILED(library);
  }
}

TEST(VersioningTests, GoodLibraryAddedAndDeprecatedAndRemoved) {
  auto source = R"FIDL(
@available(added=1, deprecated=2, removed=HEAD)
library example;
)FIDL";

  for (auto version : {"1", "2", kMaxNumericVersion.c_str(), "HEAD", "LEGACY"}) {
    TestLibrary library(source);
    library.SelectVersion("example", version);
    ASSERT_COMPILED(library);
  }
}

TEST(VersioningTests, GoodLibraryAddedAndRemovedLegacyFalse) {
  auto source = R"FIDL(
@available(added=1, removed=2, legacy=false)
library example;
)FIDL";

  for (auto version : {"1", "2", kMaxNumericVersion.c_str(), "HEAD", "LEGACY"}) {
    TestLibrary library(source);
    library.SelectVersion("example", version);
    ASSERT_COMPILED(library);
  }
}

TEST(VersioningTests, GoodLibraryAddedAndRemovedLegacyTrue) {
  auto source = R"FIDL(
@available(added=1, removed=2, legacy=true)
library example;
)FIDL";

  for (auto version : {"1", "2", kMaxNumericVersion.c_str(), "HEAD", "LEGACY"}) {
    TestLibrary library(source);
    library.SelectVersion("example", version);
    ASSERT_COMPILED(library);
  }
}

TEST(VersioningTests, GoodDeclAddedAtHead) {
  auto source = R"FIDL(
@available(added=1)
library example;

@available(added=HEAD)
type Foo = struct {};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
}

TEST(VersioningTests, GoodDeclAddedAtOne) {
  auto source = R"FIDL(
@available(added=1)
library example;

@available(added=1)
type Foo = struct {};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
}

TEST(VersioningTests, GoodDeclAddedAndRemoved) {
  auto source = R"FIDL(
@available(added=1)
library example;

@available(added=1, removed=2)
type Foo = struct {};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
}

TEST(VersioningTests, GoodDeclAddedAndDeprecatedAndRemoved) {
  auto source = R"FIDL(
@available(added=1)
library example;

@available(added=1, deprecated=2, removed=HEAD)
type Foo = struct {};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    EXPECT_FALSE(library.LookupStruct("Foo")->availability.is_deprecated());
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    EXPECT_TRUE(library.LookupStruct("Foo")->availability.is_deprecated());
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    EXPECT_TRUE(library.LookupStruct("Foo")->availability.is_deprecated());
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
}

TEST(VersioningTests, GoodDeclAddedAndRemovedLegacy) {
  auto source = R"FIDL(
@available(added=1)
library example;

@available(added=1, removed=2, legacy=true)
type Foo = struct {};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    // The decl is re-added at LEGACY.
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
}

TEST(VersioningTests, GoodMemberAddedAtHead) {
  auto source = R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(added=HEAD)
    member string;
};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
}

TEST(VersioningTests, GoodMemberAddedAtOne) {
  auto source = R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(added=1)
    member string;
};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
}

TEST(VersioningTests, GoodMemberAddedAndRemoved) {
  auto source = R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(added=1, removed=2)
    member string;
};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
}

TEST(VersioningTests, GoodMemberAddedAndDeprecatedAndRemoved) {
  auto source = R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(added=1, deprecated=2, removed=HEAD)
    member string;
};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
    EXPECT_FALSE(library.LookupStruct("Foo")->members.front().availability.is_deprecated());
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
    EXPECT_TRUE(library.LookupStruct("Foo")->members.front().availability.is_deprecated());
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
    EXPECT_TRUE(library.LookupStruct("Foo")->members.front().availability.is_deprecated());
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
}

TEST(VersioningTests, GoodMemberAddedAndRemovedLegacy) {
  auto source = R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(added=1, removed=2, legacy=true)
    member string;
};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", kMaxNumericVersion);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "HEAD");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "LEGACY");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    // The member is re-added at LEGACY.
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 1);
  }
  {
    TestLibrary library(source);
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
    ASSERT_EQ(library.LookupStruct("Foo")->members.size(), 0);
  }
}

TEST(VersioningTests, GoodAllArgumentsOnLibrary) {
  TestLibrary library(R"FIDL(
@available(platform="notexample", added=1, deprecated=2, removed=3, note="use xyz instead", legacy=false)
library example;
)FIDL");
  library.SelectVersion("notexample", "1");
  ASSERT_COMPILED(library);
}

TEST(VersioningTests, GoodAllArgumentsOnDecl) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(added=1, deprecated=2, removed=3, note="use xyz instead", legacy=false)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "1");
  ASSERT_COMPILED(library);
}

TEST(VersioningTests, GoodAllArgumentsOnMember) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(added=1, deprecated=2, removed=3, note="use xyz instead", legacy=false)
    member string;
};
)FIDL");
  library.SelectVersion("example", "1");
  ASSERT_COMPILED(library);
}

TEST(VersioningTests, GoodAttributeOnEverything) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(added=1)
const CONST uint32 = 1;

@available(added=1)
alias Alias = string;

// TODO(fxbug.dev/7807): Uncomment.
// @available(added=1)
// type Type = string;

@available(added=1)
type Bits = bits {
    @available(added=1)
    MEMBER = 1;
};

@available(added=1)
type Enum = enum {
    @available(added=1)
    MEMBER = 1;
};

@available(added=1)
type Struct = struct {
    @available(added=1)
    member string;
};

@available(added=1)
type Table = table {
    @available(added=1)
    1: reserved;
    @available(added=1)
    2: member string;
};

@available(added=1)
type Union = union {
    @available(added=1)
    1: reserved;
    @available(added=1)
    2: member string;
};

@available(added=1)
protocol ProtocolToCompose {};

@available(added=1)
protocol Protocol {
    @available(added=1)
    compose ProtocolToCompose;

    @available(added=1)
    Method() -> ();
};

@available(added=1)
service Service {
    @available(added=1)
    member client_end:Protocol;
};

@available(added=1)
resource_definition Resource : uint32 {
    properties {
        @available(added=1)
        property uint32;
    };
};
)FIDL");
  library.SelectVersion("example", "1");
  ASSERT_COMPILED(library);

  auto& unfiltered_decls = library.LookupLibrary("example")->declaration_order;
  auto& filtered_decls = library.declaration_order();
  // Because everything has the same availability, nothing gets split.
  EXPECT_EQ(unfiltered_decls.size(), filtered_decls.size());
}

// TODO(fxbug.dev/67858): Currently attributes `@HERE type Foo = struct {};` and
// `type Foo = @HERE struct {};` are interchangeable. We just disallow using
// both at once (ErrRedundantAttributePlacement). However, @available on the
// anonymous layout is confusing so maybe we should rethink this design.
TEST(VersioningTests, GoodAttributeOnAnonymousLayoutTopLevel) {
  auto source = R"FIDL(
@available(added=1)
library example;

type Foo = @available(added=2) struct {};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);
    ASSERT_NULL(library.LookupStruct("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);
    ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  }
}

TEST(VersioningTests, BadAttributeOnAnonymousLayoutInMember) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    member @available(added=2) struct {};
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(VersioningTests, BadInvalidVersionBelowMin) {
  TestLibrary library(R"FIDL(
@available(added=0)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidVersion);
}

TEST(VersioningTests, BadInvalidVersionAboveMaxNumeric) {
  TestLibrary library(R"FIDL(
@available(added=9223372036854775808) // 2^63
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidVersion);
}

TEST(VersioningTests, BadInvalidVersionBeforeHeadOrdinal) {
  TestLibrary library(R"FIDL(
@available(added=18446744073709551613) // 2^64-3
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidVersion);
}

TEST(VersioningTests, GoodVersionHeadOrdinal) {
  TestLibrary library(R"FIDL(
@available(added=18446744073709551614) // 2^64-2
library example;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(VersioningTests, BadInvalidVersionLegacyOrdinal) {
  TestLibrary library(R"FIDL(
@available(added=18446744073709551615) // 2^64-1
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidVersion);
}

TEST(VersioningTests, BadInvalidVersionAfterLegacyOrdinal) {
  TestLibrary library(R"FIDL(
@available(added=18446744073709551616) // 2^64
library example;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(VersioningTests, BadInvalidVersionLegacy) {
  TestLibrary library(R"FIDL(
@available(added=LEGACY)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgRequiresLiteral);
}

TEST(VersioningTests, BadInvalidVersionNegative) {
  TestLibrary library(R"FIDL(
@available(added=-1)
library example;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(VersioningTests, BadNoArguments) {
  TestLibrary library(R"FIDL(
@available
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailableMissingArguments);
}

TEST(VersioningTests, BadLibraryMissingAdded) {
  TestLibrary library(R"FIDL(
@available(removed=2)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrLibraryAvailabilityMissingAdded);
}

TEST(VersioningTests, BadNoteWithoutDeprecation) {
  TestLibrary library(R"FIDL(
@available(added=1, note="no need for a note")
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNoteWithoutDeprecation);
}

TEST(VersioningTests, BadPlatformNotOnLibrary) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(platform="bad")
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrPlatformNotOnLibrary);
}

TEST(VersioningTests, BadUseInUnversionedLibrary) {
  TestLibrary library(R"FIDL(
library example;

@available(added=1)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingLibraryAvailability);
}

TEST(VersioningTests, BadUseInUnversionedLibraryReportedOncePerAttribute) {
  TestLibrary library(R"FIDL(
library example;

@available(added=1)
type Foo = struct {
    @available(added=2)
    member1 bool;
    member2 bool;
};
)FIDL");
  // Note: Only twice, not a third time for member2.
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMissingLibraryAvailability,
                                      fidl::ErrMissingLibraryAvailability);
}

TEST(VersioningTests, BadAddedEqualsRemoved) {
  TestLibrary library(R"FIDL(
@available(added=1, removed=1)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAvailabilityOrder);
}

TEST(VersioningTests, BadAddedGreaterThanRemoved) {
  TestLibrary library(R"FIDL(
@available(added=2, removed=1)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAvailabilityOrder);
}

TEST(VersioningTests, GoodAddedEqualsDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=1, deprecated=1)
library example;
)FIDL");
  library.SelectVersion("example", "1");
  ASSERT_COMPILED(library);
}

TEST(VersioningTests, BadAddedGreaterThanDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=1)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAvailabilityOrder);
}

TEST(VersioningTests, BadDeprecatedEqualsRemoved) {
  TestLibrary library(R"FIDL(
@available(added=1, deprecated=2, removed=2)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAvailabilityOrder);
}

TEST(VersioningTests, BadDeprecatedGreaterThanRemoved) {
  TestLibrary library(R"FIDL(
@available(added=1, deprecated=3, removed=2)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAvailabilityOrder);
}

TEST(VersioningTests, BadLegacyTrueNotRemoved) {
  TestLibrary library(R"FIDL(
@available(added=1, legacy=true)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrLegacyWithoutRemoval);
}

TEST(VersioningTests, BadLegacyFalseNotRemoved) {
  TestLibrary library(R"FIDL(
@available(added=1, legacy=false)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrLegacyWithoutRemoval);
}

TEST(VersioningTests, GoodRedundantWithParent) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(added=2, deprecated=4, removed=6)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "2");
  ASSERT_COMPILED(library);
}

TEST(VersioningTests, BadAddedBeforeParentAdded) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(added=1)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "cannot be added before its parent element is added");
}

TEST(VersioningTests, GoodAddedWhenParentDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(added=4)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "4");
  ASSERT_COMPILED(library);

  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  EXPECT_TRUE(foo->availability.is_deprecated());
}

TEST(VersioningTests, GoodAddedAfterParentDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(added=5)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "5");
  ASSERT_COMPILED(library);

  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  EXPECT_TRUE(foo->availability.is_deprecated());
}

TEST(VersioningTests, BadAddedWhenParentRemoved) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(added=6)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "cannot be added after its parent element is removed");
}

TEST(VersioningTests, BadAddedAfterParentRemoved) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(added=7)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "cannot be added after its parent element is removed");
}

TEST(VersioningTests, BadDeprecatedBeforeParentAdded) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(deprecated=1)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg,
                "cannot be deprecated before its parent element is added");
}

TEST(VersioningTests, GoodDeprecatedWhenParentAdded) {
  TestLibrary library(R"FIDL(
@available(added=2, removed=6) // never deprecated
library example;

@available(deprecated=2)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "2");
  ASSERT_COMPILED(library);

  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  EXPECT_TRUE(foo->availability.is_deprecated());
}

TEST(VersioningTests, GoodDeprecatedBeforeParentDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(deprecated=3)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "3");
  ASSERT_COMPILED(library);

  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  EXPECT_TRUE(foo->availability.is_deprecated());
}

TEST(VersioningTests, BadDeprecatedAfterParentDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(deprecated=5)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg,
                "cannot be deprecated after its parent element is deprecated");
}

TEST(VersioningTests, BadDeprecatedWhenParentRemoved) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(deprecated=6)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg,
                "cannot be deprecated after its parent element is removed");
}

TEST(VersioningTests, BadDeprecatedAfterParentRemoved) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(deprecated=7)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg,
                "cannot be deprecated after its parent element is removed");
}

TEST(VersioningTests, BadRemovedBeforeParentAdded) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(removed=1)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "cannot be removed before its parent element is added");
}

TEST(VersioningTests, BadRemovedWhenParentAdded) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(removed=2)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "cannot be removed before its parent element is added");
}

TEST(VersioningTests, GoodRemovedBeforeParentDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(removed=3)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "2");
  ASSERT_COMPILED(library);

  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  EXPECT_FALSE(foo->availability.is_deprecated());
}

TEST(VersioningTests, GoodRemovedWhenParentDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(removed=4)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "3");
  ASSERT_COMPILED(library);

  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  EXPECT_FALSE(foo->availability.is_deprecated());
}

TEST(VersioningTests, GoodRemovedAfterParentDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(removed=5)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "4");
  ASSERT_COMPILED(library);

  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  EXPECT_TRUE(foo->availability.is_deprecated());
}

TEST(VersioningTests, BadRemovedAfterParentRemoved) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6)
library example;

@available(removed=7)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "cannot be removed after its parent element is removed");
}

TEST(VersioningTests, GoodLegacyParentNotRemovedChildFalse) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4)
library example;

@available(removed=6, legacy=false)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "LEGACY");
  ASSERT_COMPILED(library);
  ASSERT_NULL(library.LookupStruct("Foo"));
}

TEST(VersioningTests, GoodLegacyParentNotRemovedChildTrue) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4)
library example;

@available(removed=6, legacy=true)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "LEGACY");
  ASSERT_COMPILED(library);
  ASSERT_NOT_NULL(library.LookupStruct("Foo"));
}

TEST(VersioningTests, GoodLegacyParentFalseChildFalse) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6, legacy=false)
library example;

@available(legacy=false)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "LEGACY");
  ASSERT_COMPILED(library);
  ASSERT_NULL(library.LookupStruct("Foo"));
}

TEST(VersioningTests, BadLegacyParentFalseChildTrue) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6, legacy=false)
library example;

@available(legacy=true)
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrLegacyConflictsWithParent);
}

TEST(VersioningTests, GoodLegacyParentTrueChildTrue) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6, legacy=true)
library example;

@available(legacy=true)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "LEGACY");
  ASSERT_COMPILED(library);
  ASSERT_NOT_NULL(library.LookupStruct("Foo"));
}

TEST(VersioningTests, GoodLegacyParentTrueChildFalse) {
  TestLibrary library(R"FIDL(
@available(added=2, deprecated=4, removed=6, legacy=true)
library example;

@available(legacy=false)
type Foo = struct {};
)FIDL");
  library.SelectVersion("example", "LEGACY");
  ASSERT_COMPILED(library);
  ASSERT_NULL(library.LookupStruct("Foo"));
}

TEST(VersioningTests, GoodMemberInheritsFromParent) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(added=2)
type Foo = struct {
    @available(deprecated=3)
    member1 bool;
};
)FIDL");
  library.SelectVersion("example", "2");
  ASSERT_COMPILED(library);

  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  EXPECT_EQ(foo->members.size(), 1);
}

TEST(VersioningTests, GoodComplexInheritance) {
  // The following libraries all define a struct Bar with effective availability
  // @available(added=2, deprecated=3, removed=4, legacy=true) in different ways.

  std::vector<const char*> sources;

  // Direct annotation.
  sources.push_back(R"FIDL(
@available(added=1)
library example;

@available(added=2, deprecated=3, removed=4, legacy=true)
type Bar = struct {};
)FIDL");

  // Fully inherit from library declaration.
  sources.push_back(R"FIDL(
@available(added=2, deprecated=3, removed=4, legacy=true)
library example;

type Bar = struct {};
)FIDL");

  // Partially inherit from library declaration.
  sources.push_back(R"FIDL(
@available(added=1, deprecated=3)
library example;

@available(added=2, removed=4, legacy=true)
type Bar = struct {};
)FIDL");

  // Inherit from parent.
  sources.push_back(R"FIDL(
@available(added=1)
library example;

@available(added=2, deprecated=3, removed=4, legacy=true)
type Foo = struct {
    member @generated_name("Bar") struct {};
};
)FIDL");

  // Inherit from member.
  sources.push_back(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(added=2, deprecated=3, removed=4, legacy=true)
    member @generated_name("Bar") struct {};
};
)FIDL");

  // Inherit from multiple, forward.
  sources.push_back(R"FIDL(
@available(added=2)
library example;

@available(deprecated=3)
type Foo = struct {
    @available(removed=4, legacy=true)
    member @generated_name("Bar") struct {};
};
)FIDL");

  // Inherit from multiple, backward.
  sources.push_back(R"FIDL(
@available(added=1, removed=4, legacy=true)
library example;

@available(deprecated=3)
type Foo = struct {
    @available(added=2)
    member @generated_name("Bar") struct {};
};
)FIDL");

  // Inherit from multiple, mixed.
  sources.push_back(R"FIDL(
@available(added=1)
library example;

@available(added=2)
type Foo = struct {
    @available(deprecated=3, removed=4, legacy=true)
    member @generated_name("Bar") struct {};
};
)FIDL");

  // Inherit via nested layouts.
  sources.push_back(R"FIDL(
@available(added=1)
library example;

@available(added=2)
type Foo = struct {
    @available(deprecated=3)
    member1 struct {
        @available(removed=4, legacy=true)
        member2 struct {
            member3 @generated_name("Bar") struct {};
        };
    };
};
)FIDL");

  // Inherit via nested type constructors.
  sources.push_back(R"FIDL(
@available(added=1)
library example;

@available(added=2)
type Foo = struct {
    @available(deprecated=3, removed=4, legacy=true)
    member1 vector<vector<vector<@generated_name("Bar") struct{}>>>;
};
)FIDL");

  for (auto& source : sources) {
    {
      TestLibrary library(source);
      library.SelectVersion("example", "1");
      ASSERT_COMPILED(library);

      auto bar = library.LookupStruct("Bar");
      ASSERT_NULL(bar);
    }
    {
      TestLibrary library(source);
      library.SelectVersion("example", "2");
      ASSERT_COMPILED(library);

      auto bar = library.LookupStruct("Bar");
      ASSERT_NOT_NULL(bar);
      EXPECT_FALSE(bar->availability.is_deprecated());
    }
    {
      TestLibrary library(source);
      library.SelectVersion("example", "3");
      ASSERT_COMPILED(library);

      auto bar = library.LookupStruct("Bar");
      ASSERT_NOT_NULL(bar);
      EXPECT_TRUE(bar->availability.is_deprecated());
    }
    {
      TestLibrary library(source);
      library.SelectVersion("example", "4");
      ASSERT_COMPILED(library);

      auto bar = library.LookupStruct("Bar");
      ASSERT_NULL(bar);
    }
    {
      TestLibrary library(source);
      library.SelectVersion("example", "LEGACY");
      ASSERT_COMPILED(library);

      auto bar = library.LookupStruct("Bar");
      ASSERT_NOT_NULL(bar);
    }
  }
}

TEST(VersioningTests, BadDeclConflictsWithParent) {
  TestLibrary library(R"FIDL( // L1
@available(added=2)           // L2
library example;              // L3
                              // L4
@available(added=1)           // L5
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "added=1 conflicts with added=2 at example.fidl:2");
  EXPECT_EQ(library.errors()[0]->span.position().line, 5);
}

TEST(VersioningTests, BadMemberConflictsWithParent) {
  TestLibrary library(R"FIDL( // L1
@available(added=1)           // L2
library example;              // L3
                              // L4
@available(added=2)           // L5
type Foo = struct {           // L6
    @available(added=1)       // L7
    member1 bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "added=1 conflicts with added=2 at example.fidl:5");
  EXPECT_EQ(library.errors()[0]->span.position().line, 7);
}

TEST(VersioningTests, BadMemberConflictsWithGrandParent) {
  TestLibrary library(R"FIDL( // L1
@available(added=2)           // L2
library example;              // L3
                              // L4
@available(removed=3)         // L5
type Foo = struct {           // L6
    @available(added=1)       // L7
    member1 bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "added=1 conflicts with added=2 at example.fidl:2");
  EXPECT_EQ(library.errors()[0]->span.position().line, 7);
}

TEST(VersioningTests, BadMemberConflictsWithGrandParentThroughAnonymous) {
  TestLibrary library(R"FIDL( // L1
@available(added=1)           // L2
library example;              // L3
                              // L4
@available(added=2)           // L5
type Foo = struct {           // L6
    member1 struct {          // L7
        @available(removed=1) // L8
        member2 bool;
    };
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAvailabilityConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "removed=1 conflicts with added=2 at example.fidl:5");
  EXPECT_EQ(library.errors()[0]->span.position().line, 8);
}

TEST(VersioningTests, BadLegacyConflictsWithRemoved) {
  TestLibrary library(R"FIDL(  // L1
@available(added=1, removed=2) // L2
library example;               // L3
                               // L4
@available(legacy=true)        // L5
type Foo = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrLegacyConflictsWithParent);
  EXPECT_SUBSTR(library.errors()[0]->msg, "legacy=true conflicts with removed=2 at example.fidl:2");
  EXPECT_EQ(library.errors()[0]->span.position().line, 5);
}

TEST(VersioningTests, GoodNonOverlappingNames) {
  auto source = R"FIDL(
@available(added=1)
library example;

@available(removed=2)
type Foo = struct {};

@available(added=2)
type Foo = table {};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);

    EXPECT_NOT_NULL(library.LookupStruct("Foo"));
    EXPECT_NULL(library.LookupTable("Foo"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);

    EXPECT_NULL(library.LookupStruct("Foo"));
    EXPECT_NOT_NULL(library.LookupTable("Foo"));
  }
}

TEST(VersioningTests, GoodNonOverlappingNamesCanonical) {
  auto source = R"FIDL(
@available(added=1)
library example;

@available(removed=2)
type foo = struct {};

@available(added=2)
type FOO = table {};
)FIDL";

  {
    TestLibrary library(source);
    library.SelectVersion("example", "1");
    ASSERT_COMPILED(library);

    EXPECT_NOT_NULL(library.LookupStruct("foo"));
    EXPECT_NULL(library.LookupTable("FOO"));
  }
  {
    TestLibrary library(source);
    library.SelectVersion("example", "2");
    ASSERT_COMPILED(library);

    EXPECT_NULL(library.LookupStruct("foo"));
    EXPECT_NOT_NULL(library.LookupTable("FOO"));
  }
}

TEST(VersioningTests, BadOverlappingNamesEqualToOther) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {};
type Foo = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(VersioningTests, BadOverlappingNamesEqualToOtherLegacy) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(removed=2, legacy=true)
type Foo = struct {};
@available(removed=2, legacy=true)
type Foo = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(VersioningTests, BadOverlappingNamesEqualToOtherCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type foo = struct {};
type FOO = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollisionCanonical);
}

TEST(VersioningTests, BadOverlappingNamesSimple) {
  TestLibrary library;
  library.AddFile("bad/fi-0036.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlap);
}

TEST(VersioningTests, GoodOverlappingNamesSimpleFixAvailability) {
  TestLibrary library;
  library.AddFile("good/fi-0036.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(VersioningTests, BadOverlappingNamesSimpleCanonical) {
  TestLibrary library;
  library.AddFile("bad/fi-0037.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlapCanonical);
}

TEST(VersioningTests, GoodOverlappingNamesSimpleCanonicalFixRename) {
  TestLibrary library;
  library.AddFile("good/fi-0037.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(VersioningTests, BadOverlappingNamesContainsOther) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {};
@available(removed=2)
type Foo = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlap);
  EXPECT_SUBSTR(library.errors()[0]->msg, "available at version 1 of platform 'example'");
}

TEST(VersioningTests, BadOverlappingNamesContainsOtherCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type foo = struct {};
@available(removed=2)
type FOO = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlapCanonical);
  EXPECT_SUBSTR(library.errors()[0]->msg, "available at version 1 of platform 'example'");
}

TEST(VersioningTests, BadOverlappingNamesIntersectsOther) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(removed=5)
type Foo = struct {};
@available(added=3)
type Foo = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlap);
  EXPECT_SUBSTR(library.errors()[0]->msg, "available from version 3 to 4 of platform 'example'");
}

TEST(VersioningTests, BadOverlappingNamesIntersectsOtherCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(removed=5)
type foo = struct {};
@available(added=3)
type FOO = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlapCanonical);
  EXPECT_SUBSTR(library.errors()[0]->msg, "available from version 3 to 4 of platform 'example'");
}

TEST(VersioningTests, BadOverlappingNamesJustAtLegacy) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(removed=2, legacy=true)
type Foo = struct {};
@available(added=2, removed=3, legacy=true)
type Foo = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlap);
  EXPECT_SUBSTR(library.errors()[0]->msg, "available at version LEGACY of platform 'example'");
}

TEST(VersioningTests, BadOverlappingNamesJustAtLegacyCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(removed=2, legacy=true)
type foo = struct {};
@available(added=2, removed=3, legacy=true)
type FOO = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlapCanonical);
  EXPECT_SUBSTR(library.errors()[0]->msg, "available at version LEGACY of platform 'example'");
}

TEST(VersioningTests, BadOverlappingNamesIntersectAtLegacy) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(removed=2, legacy=true)
type Foo = struct {};
@available(added=2)
type Foo = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlap);
  EXPECT_SUBSTR(library.errors()[0]->msg, "available at version LEGACY of platform 'example'");
}

TEST(VersioningTests, BadOverlappingNamesIntersectAtLegacyCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(removed=2, legacy=true)
type foo = struct {};
@available(added=2)
type FOO = table {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlapCanonical);
  EXPECT_SUBSTR(library.errors()[0]->msg, "available at version LEGACY of platform 'example'");
}

TEST(VersioningTests, BadOverlappingNamesMultiple) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {};
@available(added=3)
type Foo = table {};
@available(added=HEAD)
const Foo uint32 = 0;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrNameOverlap, fidl::ErrNameOverlap);
  EXPECT_SUBSTR(library.errors()[0]->msg, "available at version HEAD of platform 'example'");
  EXPECT_SUBSTR(library.errors()[1]->msg, "available from version 3 onward of platform 'example'");
}

TEST(VersioningTests, BadOverlappingNamesRecursive) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(added=1, removed=5)
type Foo = struct { member box<Foo>; };

@available(added=3, removed=7)
type Foo = struct { member box<Foo>; };
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameOverlap);
}

TEST(VersioningTests, BadOverlappingMemberNamesEqualToOther) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    member bool;
    member bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberName);
}

TEST(VersioningTests, BadOverlappingMemberNamesEqualToOtherLegacy) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(removed=2, legacy=true)
    member bool;
    @available(removed=2, legacy=true)
    member bool;
};
)FIDL");
  // Once for [1, 2), once for [LEGACY, +inf).
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberName,
                                      fidl::ErrDuplicateStructMemberName);
}

TEST(VersioningTests, BadOverlappingMemberNamesEqualToOtherCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    member bool;
    MEMBER bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberNameCanonical);
}

TEST(VersioningTests, BadOverlappingMemberNamesContainsOther) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    member bool;
    @available(removed=2)
    member bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberName);
}

TEST(VersioningTests, BadOverlappingMemberNamesContainsOtherCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    member bool;
    @available(removed=2)
    MEMBER bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberNameCanonical);
}

TEST(VersioningTests, BadOverlappingMemberNamesIntersectsOther) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(removed=5)
    member bool;
    @available(added=3)
    member bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberName);
}

TEST(VersioningTests, BadOverlappingMemberNamesIntersectsOtherCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(removed=5)
    member bool;
    @available(added=3)
    MEMBER bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberNameCanonical);
}

TEST(VersioningTests, BadOverlappingMemberNamesJustAtLegacy) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(removed=2, legacy=true)
    member bool;
    @available(added=2, removed=3, legacy=true)
    member bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberName);
}

TEST(VersioningTests, BadOverlappingMemberNamesJustAtLegacyCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(removed=2, legacy=true)
    member bool;
    @available(added=2, removed=3, legacy=true)
    MEMBER bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberNameCanonical);
}

TEST(VersioningTests, BadOverlappingMemberNamesIntersectAtLegacy) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(removed=2, legacy=true)
    member bool;
    @available(added=2)
    member bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberName);
}

TEST(VersioningTests, BadOverlappingMemberNamesIntersectAtLegacyCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    @available(removed=2, legacy=true)
    member bool;
    @available(added=2)
    MEMBER bool;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberNameCanonical);
}

TEST(VersioningTests, BadOverlappingMemberNamesMultiple) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    member bool;
    @available(added=3)
    member bool;
    @available(added=HEAD)
    member bool;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 3);
  EXPECT_ERR(library.errors()[0], fidl::ErrDuplicateStructMemberName);
  EXPECT_ERR(library.errors()[1], fidl::ErrDuplicateStructMemberName);
  EXPECT_ERR(library.errors()[2], fidl::ErrDuplicateStructMemberName);
}

TEST(VersioningTests, BadOverlappingMemberNamesMultipleCanonical) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

type Foo = struct {
    member bool;
    @available(added=3)
    Member bool;
    @available(added=HEAD)
    MEMBER bool;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 3);
  EXPECT_ERR(library.errors()[0], fidl::ErrDuplicateStructMemberNameCanonical);
  EXPECT_ERR(library.errors()[1], fidl::ErrDuplicateStructMemberNameCanonical);
  EXPECT_ERR(library.errors()[2], fidl::ErrDuplicateStructMemberNameCanonical);
}

// TODO(fxbug.dev/101849): Generalize this with more comprehensive tests in
// availability_interleaving_tests.cc.
TEST(VersioningTests, GoodRegularDeprecatedReferencesVersionedDeprecated) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@deprecated
const FOO uint32 = BAR;
@available(deprecated=1)
const BAR uint32 = 1;
)FIDL");
  ASSERT_COMPILED(library);
}

// Previously this errored due to incorrect logic in deprecation checks.
TEST(VersioningTests, GoodDeprecationLogicRegression1) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(deprecated=1, removed=3)
type Foo = struct {};

@available(deprecated=1, removed=3)
type Bar = struct {
    foo Foo;
    @available(added=2)
    ensure_split_at_v2 string;
};
)FIDL");
  ASSERT_COMPILED(library);
}

// Previously this crashed due to incorrect logic in deprecation checks.
TEST(VersioningTests, BadDeprecationLogicRegression2) {
  TestLibrary library(R"FIDL(
@available(added=1)
library example;

@available(deprecated=1)
type Foo = struct {};

@available(deprecated=1, removed=3)
type Bar = struct {
    foo Foo;
    @available(added=2)
    ensure_split_at_v2 string;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(VersioningTests, GoodMultipleFiles) {
  TestLibrary library;
  library.AddSource("overview.fidl", R"FIDL(
/// Some doc comment.
@available(added=1)
library example;
)FIDL");
  library.AddSource("first.fidl", R"FIDL(
library example;

@available(added=2)
type Foo = struct {
    bar box<Bar>;
};
)FIDL");
  library.AddSource("second.fidl", R"FIDL(
library example;

@available(added=2)
type Bar = struct {
    foo box<Foo>;
};
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_NOT_NULL(library.LookupStruct("Foo"));
  ASSERT_NOT_NULL(library.LookupStruct("Bar"));
}

TEST(VersioningTests, GoodSplitByDeclInExternalLibrary) {
  SharedAmongstLibraries shared;

  TestLibrary dependency(&shared, "dependency.fidl", R"FIDL(
@available(added=1)
library platform.dependency;

type Foo = struct {
    @available(added=2)
    member string;
};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary example(&shared, "example.fidl", R"FIDL(
@available(added=1)
library platform.example;

using platform.dependency;

type ShouldBeSplit = struct {
    foo platform.dependency.Foo;
};
)FIDL");
  ASSERT_COMPILED(example);
}

TEST(VersioningTests, GoodMultiplePlatformsBasic) {
  SharedAmongstLibraries shared;
  shared.SelectVersion("dependency", "3");
  shared.SelectVersion("example", "HEAD");

  TestLibrary dependency(&shared, "dependency.fidl", R"FIDL(
@available(added=2)
library dependency;

@available(added=3, deprecated=4, removed=5)
type Foo = struct {};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary example(&shared, "example.fidl", R"FIDL(
@available(added=1)
library example;

using dependency;

type Foo = struct {
    @available(deprecated=5)
    dep dependency.Foo;
};
)FIDL");
  ASSERT_COMPILED(example);
}

TEST(VersioningTests, GoodMultiplePlatformsExplicitPlatform) {
  SharedAmongstLibraries shared;
  shared.SelectVersion("xyz", "3");
  shared.SelectVersion("example", "HEAD");

  TestLibrary dependency(&shared, "dependency.fidl", R"FIDL(
@available(platform="xyz", added=1)
library dependency;

@available(added=3, removed=4)
type Foo = struct {};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary example(&shared, "example.fidl", R"FIDL(
@available(added=1)
library example;

using dependency;

alias Foo = dependency.Foo;
)FIDL");
  ASSERT_COMPILED(example);
}

TEST(VersioningTests, GoodMultiplePlatformsUsesCorrectDecl) {
  SharedAmongstLibraries shared;
  shared.SelectVersion("dependency", "4");
  shared.SelectVersion("example", "1");

  TestLibrary dependency(&shared, "dependency.fidl", R"FIDL(
@available(added=2)
library dependency;

@available(deprecated=3, removed=4)
type Foo = resource struct {};

@available(added=4, removed=5)
type Foo = table {};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary example(&shared, "example.fidl", R"FIDL(
@available(added=1)
library example;

using dependency;

type Foo = struct {
    dep dependency.Foo;
};
)FIDL");
  ASSERT_COMPILED(example);

  auto foo = example.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  ASSERT_EQ(foo->members.size(), 1);
  auto member_type = foo->members[0].type_ctor->type;
  ASSERT_EQ(member_type->kind, fidl::flat::Type::Kind::kIdentifier);
  auto identifier_type = static_cast<const fidl::flat::IdentifierType*>(member_type);
  EXPECT_EQ(identifier_type->type_decl->kind, fidl::flat::Decl::Kind::kTable);
}

TEST(VersioningTests, BadMultiplePlatformsNameNotFound) {
  SharedAmongstLibraries shared;
  shared.SelectVersion("dependency", "HEAD");
  shared.SelectVersion("example", "HEAD");

  TestLibrary dependency(&shared, "dependency.fidl", R"FIDL(
@available(added=2)
library dependency;

@available(added=3, removed=5)
type Foo = struct {};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary example(&shared, "example.fidl", R"FIDL(
@available(added=1)
library example;

using dependency;

type Foo = struct {
    @available(deprecated=5)
    dep dependency.Foo;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(example, fidl::ErrNameNotFound, fidl::ErrNameNotFound);
}

}  // namespace
