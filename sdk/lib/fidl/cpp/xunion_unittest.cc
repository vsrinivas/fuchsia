// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
#include <lib/fidl/cpp/comparison.h>

#include "gtest/gtest.h"

namespace fidl {
namespace {

TEST(XUnion, SetterReturnsSelf) {
  test::misc::SampleXUnion u;
  test::misc::SimpleTable st;
  st.set_x(42);
  u.set_st(std::move(st));

  EXPECT_TRUE(u.is_st());
  EXPECT_EQ(42, u.st().x());

  EXPECT_TRUE(fidl::Equals(u, test::misc::SampleXUnion().set_st(std::move(
                                  test::misc::SimpleTable().set_x(42)))));
}

}  // namespace
}  // namespace fidl
