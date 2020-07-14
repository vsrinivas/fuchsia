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

using fidl::flat::HandleType;
using fidl::flat::Name;
using fidl::types::HandleSubtype;
using fidl::types::Nullability;

TEST(FlatAstTests, implicit_assumptions) {
  // Preconditions to unit test cases: if these change, we need to rewrite the tests themselves.
  EXPECT_TRUE(HandleSubtype::kChannel < HandleSubtype::kEvent);
  EXPECT_TRUE(Nullability::kNullable < Nullability::kNonnullable);
}

TEST(FlatAstTests, compare_handles) {
  auto name_not_important = Name::CreateIntrinsic("ignore");
  HandleType nonnullable_channel(name_not_important, HandleSubtype::kChannel, nullptr,
                                 Nullability::kNonnullable);
  HandleType nullable_channel(name_not_important, HandleSubtype::kChannel, nullptr,
                              Nullability::kNullable);
  HandleType nonnullable_event(name_not_important, HandleSubtype::kEvent, nullptr,
                               Nullability::kNonnullable);
  HandleType nullable_event(name_not_important, HandleSubtype::kEvent, nullptr,
                            Nullability::kNullable);

  // Comparison is nullability, then type.
  EXPECT_TRUE(nullable_channel < nonnullable_channel);
  EXPECT_TRUE(nullable_event < nonnullable_event);
  EXPECT_TRUE(nonnullable_channel < nonnullable_event);
  EXPECT_TRUE(nullable_channel < nullable_event);
}

}  // namespace
