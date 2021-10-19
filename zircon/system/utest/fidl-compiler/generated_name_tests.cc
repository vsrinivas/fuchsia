// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(GeneratedNameTests, BadOverrideAttributePlacements) {
  {
    TestLibrary library(R"FIDL(
library fidl.test;

@generated_name("Good")
type Bad = struct {};

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  }
  {
    TestLibrary library(R"FIDL(
library fidl.test;

type Bad = @generated_name("Good") struct {};

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

type MetaVars = enum {
  FOO = 1;
  @generated_name("BAZ")
  BAR = 2;
}

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {};

service Bar {
  @generated_name("One")
  bar_one client_end:Bar;
}

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
  }
}

TEST(GeneratedNameTests, BadMissingOverrideArg) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bad @generated_name struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingRequiredAnonymousAttributeArg);
}

TEST(GeneratedNameTests, BadOverrideValue) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bad @generated_name("ez$") struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidNameOverride);
}

TEST(GeneratedNameTests, BadOverrideCausesNameConflict) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  foo @generated_name("Baz") struct {};
};

type Baz = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(GeneratedNameTests, BadGeneratedNameFromConstant) {
  TestLibrary library(R"FIDL(
library fidl.test;

const NAME string = "baz";

type Foo = struct {
  bar @generated_name(NAME) struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgDisallowsConstants);
}

}  // namespace
