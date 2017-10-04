// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "garnet/bin/ui/ime/text_input_state_update_functions.h"

namespace ime {
namespace test {

TEST(TestInputState, DeleteBackward) {
  auto state = mozart::TextInputState::New();
  state->selection = mozart::TextSelection::New();
  auto& revision = state->revision;
  auto& base = state->selection->base;
  auto& extent = state->selection->extent;
  auto& text = state->text;

  revision = 0 + 1;
  base = extent = -1;
  text = fidl::String("");

  DeleteBackward(state);
  EXPECT_EQ(2U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);

  base = extent = 0;
  DeleteBackward(state);
  EXPECT_EQ(3U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);

  text = fidl::String("abcdefghi");
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
  EXPECT_EQ("bcdefghi", text.To<std::string>());

  base = 2;
  extent = 4;
  DeleteBackward(state);
  EXPECT_EQ(6U, revision);
  EXPECT_EQ(2, base);
  EXPECT_EQ(2, extent);
  EXPECT_EQ("bcfghi", text.To<std::string>());

  DeleteBackward(state);
  EXPECT_EQ(7U, revision);
  EXPECT_EQ(1, base);
  EXPECT_EQ(1, extent);
  EXPECT_EQ("bfghi", text.To<std::string>());

  DeleteBackward(state);
  EXPECT_EQ(8U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);
  EXPECT_EQ("fghi", text.To<std::string>());

  DeleteBackward(state);
  EXPECT_EQ(9U, revision);
  EXPECT_EQ(0, base);
  EXPECT_EQ(0, extent);
  EXPECT_EQ("fghi", text.To<std::string>());

  base = -1;
  extent = -1;
  DeleteBackward(state);
  EXPECT_EQ(10U, revision);
  EXPECT_EQ(3, base);
  EXPECT_EQ(3, extent);
  EXPECT_EQ("fgh", text.To<std::string>());

  DeleteBackward(state);
  EXPECT_EQ(11U, revision);
  EXPECT_EQ(2, base);
  EXPECT_EQ(2, extent);
  EXPECT_EQ("fg", text.To<std::string>());
}

}  // namespace test
}  // namespace ime
