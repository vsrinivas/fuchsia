// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <regex>

#include <fidl/flat_ast.h>
#include <zxtest/zxtest.h>

namespace {

using fidl::SourceSpan;
using fidl::flat::Constant;
using fidl::flat::HandleType;
using fidl::flat::Name;
using fidl::flat::NumericConstantValue;
using fidl::types::HandleSubtype;
using fidl::types::Nullability;
using fidl::types::Rights;

TEST(FlatAstTests, implicit_assumptions) {
  // Preconditions to unit test cases: if these change, we need to rewrite the tests themselves.
  EXPECT_TRUE(HandleSubtype::kChannel < HandleSubtype::kEvent);
  EXPECT_TRUE(Nullability::kNullable < Nullability::kNonnullable);
}

TEST(FlatAstTests, compare_handles) {
  auto name_not_important = Name::CreateIntrinsic("ignore");
  auto rights1Constant = std::make_unique<Constant>(Constant::Kind::kLiteral, SourceSpan());
  rights1Constant->ResolveTo(std::make_unique<NumericConstantValue<Rights>>(1));
  auto rights2Constant = std::make_unique<Constant>(Constant::Kind::kLiteral, SourceSpan());
  rights2Constant->ResolveTo(std::make_unique<NumericConstantValue<Rights>>(2));
  HandleType nonnullable_channel_rights1(name_not_important, 4, HandleSubtype::kChannel,
                                         rights1Constant.get(), Nullability::kNonnullable);
  HandleType nullable_channel_rights1(name_not_important, 4, HandleSubtype::kChannel,
                                      rights1Constant.get(), Nullability::kNullable);
  HandleType nonnullable_event_rights1(name_not_important, 5, HandleSubtype::kEvent,
                                       rights1Constant.get(), Nullability::kNonnullable);
  HandleType nullable_event_rights1(name_not_important, 5, HandleSubtype::kEvent,
                                    rights1Constant.get(), Nullability::kNullable);
  HandleType nullable_event_rights2(name_not_important, 5, HandleSubtype::kEvent,
                                    rights2Constant.get(), Nullability::kNullable);

  // Comparison is nullability, then type.
  EXPECT_TRUE(nullable_channel_rights1 < nonnullable_channel_rights1);
  EXPECT_TRUE(nullable_event_rights1 < nonnullable_event_rights1);
  EXPECT_TRUE(nonnullable_channel_rights1 < nonnullable_event_rights1);
  EXPECT_TRUE(nullable_channel_rights1 < nullable_event_rights1);
  EXPECT_TRUE(nullable_event_rights1 < nullable_event_rights2);
}

}  // namespace
