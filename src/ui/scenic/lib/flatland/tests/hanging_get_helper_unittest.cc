// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/hanging_get_helper.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

using fuchsia::ui::scenic::internal::GraphLinkStatus;
using fuchsia::ui::scenic::internal::LayoutInfo;
using fuchsia::ui::scenic::internal::Vec2;

namespace flatland {
namespace test {

TEST(HangingGetHelperTest, HangingGetProducesValidResponse) {
  HangingGetHelper<Vec2> helper;

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(Vec2{1.0f, 2.0f});

  EXPECT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));
}

TEST(HangingGetHelperTest, NonHangingGetProducesValidResponse) {
  HangingGetHelper<Vec2> helper;

  helper.Update(Vec2{1.0f, 2.0f});

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));
}

TEST(HangingGetHelperTest, DataOverrideResultsInFinalValue) {
  HangingGetHelper<Vec2> helper;

  helper.Update(Vec2{1.0f, 2.0f});
  helper.Update(Vec2{3.0f, 4.0f});
  helper.Update(Vec2{5.0f, 6.0f});

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{5.0f, 6.0f}, data.value()));
}

TEST(HangingGetHelperTest, DataOverrideInBatches) {
  HangingGetHelper<Vec2> helper;

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(Vec2{1.0f, 2.0f});
  EXPECT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));

  helper.Update(Vec2{3.0f, 4.0f});
  helper.Update(Vec2{5.0f, 6.0f});

  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));
  data.reset();
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{5.0f, 6.0f}, data.value()));
}

TEST(HangingGetHelperTest, DuplicateDataIsIgnored) {
  HangingGetHelper<Vec2> helper;

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(Vec2{1.0f, 2.0f});
  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));

  data.reset();
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(Vec2{1.0f, 2.0f});
  EXPECT_FALSE(data);

  helper.Update(Vec2{3.0f, 4.0f});
  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{3.0f, 4.0f}, data.value()));
}

TEST(HangingGetHelperTest, EnumDuplicateDataIsIgnored) {
  HangingGetHelper<GraphLinkStatus> helper;
  std::optional<GraphLinkStatus> data;
  helper.SetCallback([&](GraphLinkStatus d) { data = std::move(d); });
  EXPECT_FALSE(data);

  helper.Update(GraphLinkStatus::CONNECTED_TO_DISPLAY);
  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(GraphLinkStatus::CONNECTED_TO_DISPLAY, data.value()));

  data.reset();
  helper.SetCallback([&](GraphLinkStatus d) { data = std::move(d); });
  EXPECT_FALSE(data);

  helper.Update(GraphLinkStatus::CONNECTED_TO_DISPLAY);
  EXPECT_FALSE(data);

  helper.Update(GraphLinkStatus::DISCONNECTED_FROM_DISPLAY);
  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(GraphLinkStatus::DISCONNECTED_FROM_DISPLAY, data.value()));
}

TEST(HangingGetHelperTest, TableDuplicateDataIsIgnored) {
  HangingGetHelper<LayoutInfo> helper;
  std::optional<LayoutInfo> data;
  helper.SetCallback([&](LayoutInfo d) { data = std::move(d); });
  EXPECT_FALSE(data);

  LayoutInfo info;
  info.set_logical_size(Vec2{1.0f, 2.0f});

  {
    LayoutInfo info2;
    info.Clone(&info2);
    helper.Update(std::move(info2));
  }
  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(info, data.value()));

  data.reset();
  helper.SetCallback([&](LayoutInfo d) { data = std::move(d); });
  EXPECT_FALSE(data);

  {
    LayoutInfo info3;
    info.Clone(&info3);
    helper.Update(std::move(info3));
  }
  EXPECT_FALSE(data);

  LayoutInfo new_info;
  new_info.set_logical_size(Vec2{3.0f, 4.0f});

  {
    LayoutInfo info4;
    new_info.Clone(&info4);
    helper.Update(std::move(info4));
  }
  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(new_info, data.value()));
}

}  // namespace test
}  // namespace flatland
