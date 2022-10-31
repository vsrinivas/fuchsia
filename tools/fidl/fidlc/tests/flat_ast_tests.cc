// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

using fidl::Token;
using fidl::flat::HandleRights;
using fidl::flat::HandleType;
using fidl::flat::LiteralConstant;
using fidl::flat::Name;
using fidl::raw::Literal;
using fidl::raw::SourceElement;
using fidl::types::HandleSubtype;
using fidl::types::Nullability;
using fidl::types::RightsWrappedType;

TEST(FlatAstTests, GoodImplicitAssumptions) {
  // Preconditions to unit test cases: if these change, we need to rewrite the tests themselves.
  EXPECT_TRUE(HandleSubtype::kChannel < HandleSubtype::kEvent);
  EXPECT_TRUE(Nullability::kNullable < Nullability::kNonnullable);
}

TEST(FlatAstTests, GoodCompareHandles) {
  auto name_not_important = Name::CreateIntrinsic(nullptr, "ignore");
  auto fake_source_element = SourceElement(Token(), Token());
  auto fake_literal = Literal(fake_source_element, Literal::Kind::kNumeric);
  auto rights1Constant = std::make_unique<LiteralConstant>(&fake_literal);
  rights1Constant->ResolveTo(std::make_unique<HandleRights>(1), nullptr);
  auto rights1Value = static_cast<const HandleRights*>(&rights1Constant->Value());
  auto rights2Constant = std::make_unique<LiteralConstant>(&fake_literal);
  rights2Constant->ResolveTo(std::make_unique<HandleRights>(2), nullptr);
  auto rights2Value = static_cast<const HandleRights*>(&rights2Constant->Value());
  fidl::flat::Resource* resource_decl_not_needed = nullptr;
  uint32_t channel_obj_type = 4;
  uint32_t event_obj_type = 5;
  HandleType nonnullable_channel_rights1(name_not_important, resource_decl_not_needed,
                                         channel_obj_type, rights1Value, Nullability::kNonnullable);
  HandleType nullable_channel_rights1(name_not_important, resource_decl_not_needed,
                                      channel_obj_type, rights1Value, Nullability::kNullable);
  HandleType nonnullable_event_rights1(name_not_important, resource_decl_not_needed, event_obj_type,
                                       rights1Value, Nullability::kNonnullable);
  HandleType nullable_event_rights1(name_not_important, resource_decl_not_needed, event_obj_type,
                                    rights1Value, Nullability::kNullable);
  HandleType nullable_event_rights2(name_not_important, resource_decl_not_needed, event_obj_type,
                                    rights2Value, Nullability::kNullable);

  // Comparison is nullability, then type.
  EXPECT_TRUE(nullable_channel_rights1 < nonnullable_channel_rights1);
  EXPECT_TRUE(nullable_event_rights1 < nonnullable_event_rights1);
  EXPECT_TRUE(nonnullable_channel_rights1 < nonnullable_event_rights1);
  EXPECT_TRUE(nullable_channel_rights1 < nullable_event_rights1);
  EXPECT_TRUE(nullable_event_rights1 < nullable_event_rights2);
}

TEST(FlatAstTests, BadCannotReferenceAnonymousName) {
  TestLibrary library;
  library.AddFile("bad/fi-0058.test.fidl");
  ASSERT_FALSE(library.Compile());

  for (const auto& err : library.errors()) {
    EXPECT_ERR(err, fidl::ErrAnonymousNameReference);
  }
}

TEST(FlatAstTests, BadAnonymousNameConflict) {
  TestLibrary library(R"FIDL(
library example;

protocol Foo {
  SomeMethod(struct { some_param uint8; });
};

type FooSomeMethodRequest = struct {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(FlatAstTests, GoodSingleAnonymousNameUse) {
  TestLibrary library(R"FIDL(library example;

protocol Foo {
    SomeMethod() -> (struct {
        some_param uint8;
    }) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(FlatAstTests, BadMultipleLibrariesSameName) {
  SharedAmongstLibraries shared;
  TestLibrary library1(&shared);
  library1.AddFile("bad/fi-0041-a.test.fidl");
  ASSERT_COMPILED(library1);
  TestLibrary library2(&shared);
  library2.AddFile("bad/fi-0041-b.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library2, fidl::ErrMultipleLibrariesWithSameName);
}

}  // namespace
