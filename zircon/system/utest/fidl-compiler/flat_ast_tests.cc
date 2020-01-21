// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <regex>

#include <fidl/flat_ast.h>
#include <unittest/unittest.h>

namespace {

using fidl::flat::HandleType;
using fidl::flat::Name;
using fidl::types::HandleSubtype;
using fidl::types::Nullability;

static bool implicit_assumptions() {
  // Preconditions to unit test cases: if these change, we need to rewrite the tests themselves.
  EXPECT_TRUE(HandleSubtype::kChannel < HandleSubtype::kEvent);
  EXPECT_TRUE(Nullability::kNullable < Nullability::kNonnullable);

  return true;
}

static bool compare_handles() {
  Name name_not_important(nullptr, "ignore");
  HandleType nonnullable_channel(name_not_important, HandleSubtype::kChannel, nullptr, nullptr,
                                 Nullability::kNonnullable);
  HandleType nullable_channel(name_not_important, HandleSubtype::kChannel, nullptr, nullptr,
                              Nullability::kNullable);
  HandleType nonnullable_event(name_not_important, HandleSubtype::kEvent, nullptr, nullptr,
                               Nullability::kNonnullable);
  HandleType nullable_event(name_not_important, HandleSubtype::kEvent, nullptr, nullptr,
                            Nullability::kNullable);

  // Comparison is nullability, then type.
  EXPECT_TRUE(nullable_channel < nonnullable_channel);
  EXPECT_TRUE(nullable_event < nonnullable_event);
  EXPECT_TRUE(nonnullable_channel < nonnullable_event);
  EXPECT_TRUE(nullable_channel < nullable_event);

  return true;
}

}  // namespace

BEGIN_TEST_CASE(flat_ast_tests)
RUN_TEST(implicit_assumptions)
RUN_TEST(compare_handles)
END_TEST_CASE(flat_ast_tests)
