// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/hanging_get_helper.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

namespace flatland {
namespace test {

TEST(HangingGetHelperTest, HangingGet) {
  HangingGetHelper<uint64_t> helper;

  std::optional<uint64_t> data;
  helper.SetCallback([&](uint64_t d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(1);

  EXPECT_TRUE(data);
  EXPECT_EQ(1u, data.value());
}

TEST(HangingGetHelperTest, NonHangingGet) {
  HangingGetHelper<uint64_t> helper;

  helper.Update(1);

  std::optional<uint64_t> data;
  helper.SetCallback([&](uint64_t d) { data = d; });
  EXPECT_TRUE(data);
  EXPECT_EQ(1u, data.value());
}

TEST(HangingGetHelperTest, DataOverride) {
  HangingGetHelper<uint64_t> helper;

  helper.Update(1);
  helper.Update(2);
  helper.Update(3);

  std::optional<uint64_t> data;
  helper.SetCallback([&](uint64_t d) { data = d; });
  EXPECT_TRUE(data);
  EXPECT_EQ(3u, data.value());
}

TEST(HangingGetHelperTest, MultipleUpdatesWithGap) {
  HangingGetHelper<uint64_t> helper;

  std::optional<uint64_t> data;
  helper.SetCallback([&](uint64_t d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(1);
  EXPECT_TRUE(data);
  EXPECT_EQ(1u, data.value());

  helper.Update(2);
  helper.Update(3);

  EXPECT_EQ(1u, data.value());
  data.reset();
  helper.SetCallback([&](uint64_t d) { data = d; });
  EXPECT_TRUE(data);
  EXPECT_EQ(3u, data.value());
}

TEST(HangingGetHelperTest, DuplicateData) {
  HangingGetHelper<uint64_t> helper;

  std::optional<uint64_t> data;
  helper.SetCallback([&](uint64_t d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(1);
  EXPECT_TRUE(data);
  EXPECT_EQ(1u, data.value());

  data.reset();
  helper.SetCallback([&](uint64_t d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(1);
  EXPECT_TRUE(data);
  EXPECT_EQ(1u, data.value());
}

}  // namespace test
}  // namespace flatland
