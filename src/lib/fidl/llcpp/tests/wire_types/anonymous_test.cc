// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.llcpp.types.test/cpp/wire.h>

#include <gtest/gtest.h>

TEST(Anonymous, ScopedAndFlattenedNames) {
  namespace test = fidl_llcpp_types_test;

  // Test that anonymous layouts can be accessed using both the flattened and
  // scoped names.

  [[maybe_unused]] test::wire::ReqMember req_member_flat;
  [[maybe_unused]] fidl::WireRequest<test::UsesAnonymous::FooMethod>::ReqMember req_member_scoped;
  bool same = std::is_same_v<test::wire::ReqMember,
                             fidl::WireRequest<test::UsesAnonymous::FooMethod>::ReqMember>;
  EXPECT_TRUE(same);

  [[maybe_unused]] test::wire::InnerTable inner_table_flat;
  [[maybe_unused]] test::wire::ReqMember::InnerTable inner_table_scoped;
  same = std::is_same_v<test::wire::InnerTable, test::wire::ReqMember::InnerTable>;
  EXPECT_TRUE(same);

  [[maybe_unused]] test::wire::UsesAnonymousFooMethodResult result_flat;
  [[maybe_unused]] fidl::WireResponse<test::UsesAnonymous::FooMethod>::Result result_scoped;
  same = std::is_same_v<test::wire::UsesAnonymousFooMethodResult,
                        fidl::WireResponse<test::UsesAnonymous::FooMethod>::Result>;
  EXPECT_TRUE(same);

  [[maybe_unused]] test::wire::UsesAnonymousFooMethodResponse resp_flat;
  [[maybe_unused]] test::wire::UsesAnonymousFooMethodResult::Response resp_scoped;
  same = std::is_same_v<test::wire::UsesAnonymousFooMethodResponse,
                        test::wire::UsesAnonymousFooMethodResult::Response>;
  EXPECT_TRUE(same);

  [[maybe_unused]] test::wire::UsesAnonymousFooMethodError err_flat;
  [[maybe_unused]] test::wire::UsesAnonymousFooMethodResult::Err err_scoped;
  same = std::is_same_v<test::wire::UsesAnonymousFooMethodError,
                        test::wire::UsesAnonymousFooMethodResult::Err>;
  EXPECT_TRUE(same);
}
