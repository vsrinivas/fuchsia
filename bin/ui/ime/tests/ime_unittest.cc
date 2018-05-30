// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "garnet/bin/ui/ime/text_input_state_update_functions.h"

namespace ime {
namespace test {

TEST(TestInputState, DeleteBackward) {
  fuchsia::ui::input::TextInputState state;
  auto& revision = state.revision;
  auto& base = state.selection.base;
  auto& extent = state.selection.extent;
  auto& text = state.text;

  revision = 0 + 1;
  base = extent = -1;
  text = fidl::StringPtr("");

  DeleteBackward(state);
  EXPECT_EQ(2U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);

  base = extent = 0;
  DeleteBackward(state);
  EXPECT_EQ(3U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);

  text = fidl::StringPtr("abcdefghi");
  DeleteBackward(state);
  EXPECT_EQ(4U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);

  base = 0;
  extent = 1;
  DeleteBackward(state);
  EXPECT_EQ(5U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);
  EXPECT_EQ("bcdefghi", *text);

  base = 2;
  extent = 4;
  DeleteBackward(state);
  EXPECT_EQ(6U, revision);
  EXPECT_EQ(2, base);
  EXPECT_EQ(2, extent);
  EXPECT_EQ("bcfghi", *text);

  DeleteBackward(state);
  EXPECT_EQ(7U, revision);
  EXPECT_EQ(1, base);
  EXPECT_EQ(1, extent);
  EXPECT_EQ("bfghi", *text);

  DeleteBackward(state);
  EXPECT_EQ(8U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);
  EXPECT_EQ("fghi", *text);

  DeleteBackward(state);
  EXPECT_EQ(9U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);
  EXPECT_EQ("fghi", *text);

  base = -1;
  extent = -1;
  DeleteBackward(state);
  EXPECT_EQ(10U, revision);
  EXPECT_EQ(3, base);
  EXPECT_EQ(3, extent);
  EXPECT_EQ("fgh", *text);

  DeleteBackward(state);
  EXPECT_EQ(11U, revision);
  EXPECT_EQ(2, base);
  EXPECT_EQ(2, extent);
  EXPECT_EQ("fg", *text);
}

}  // namespace test
}  // namespace ime
