// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/hanging_get_helper.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

using fuchsia::math::SizeU;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::ParentViewportStatus;

namespace flatland {
namespace test {

TEST(HangingGetHelperTest, HangingGetProducesValidResponse) {
  HangingGetHelper<SizeU> helper;

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });
  EXPECT_TRUE(helper.HasPendingCallback());
  EXPECT_FALSE(data);

  helper.Update(SizeU{1, 2});

  EXPECT_FALSE(helper.HasPendingCallback());
  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));
}

TEST(HangingGetHelperTest, NonHangingGetProducesValidResponse) {
  HangingGetHelper<SizeU> helper;

  helper.Update(SizeU{1, 2});

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));
}

TEST(HangingGetHelperTest, DataOverrideResultsInFinalValue) {
  HangingGetHelper<SizeU> helper;

  helper.Update(SizeU{1, 2});
  helper.Update(SizeU{3, 4});
  helper.Update(SizeU{5, 6});

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{5, 6}, data.value()));
}

TEST(HangingGetHelperTest, DataOverrideInBatches) {
  HangingGetHelper<SizeU> helper;

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(SizeU{1, 2});

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));

  helper.Update(SizeU{3, 4});
  helper.Update(SizeU{5, 6});

  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));
  data.reset();
  helper.SetCallback([&](SizeU d) { data = d; });

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{5, 6}, data.value()));
}

TEST(HangingGetHelperTest, DuplicateDataIsIgnored) {
  HangingGetHelper<SizeU> helper;

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(SizeU{1, 2});

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));

  data.reset();
  helper.SetCallback([&](SizeU d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(SizeU{1, 2});
  EXPECT_FALSE(data);

  helper.Update(SizeU{3, 4});

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{3, 4}, data.value()));
}

TEST(HangingGetHelperTest, EnumDuplicateDataIsIgnored) {
  HangingGetHelper<ParentViewportStatus> helper;

  std::optional<ParentViewportStatus> data;
  helper.SetCallback([&](ParentViewportStatus d) { data = std::move(d); });

  EXPECT_FALSE(data);

  helper.Update(ParentViewportStatus::CONNECTED_TO_DISPLAY);

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(ParentViewportStatus::CONNECTED_TO_DISPLAY, data.value()));

  data.reset();
  helper.SetCallback([&](ParentViewportStatus d) { data = std::move(d); });
  EXPECT_FALSE(data);

  helper.Update(ParentViewportStatus::CONNECTED_TO_DISPLAY);
  EXPECT_FALSE(data);

  helper.Update(ParentViewportStatus::DISCONNECTED_FROM_DISPLAY);

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(ParentViewportStatus::DISCONNECTED_FROM_DISPLAY, data.value()));
}

TEST(HangingGetHelperTest, TableDuplicateDataIsIgnored) {
  HangingGetHelper<LayoutInfo> helper;

  std::optional<LayoutInfo> data;
  helper.SetCallback([&](LayoutInfo d) { data = std::move(d); });

  EXPECT_FALSE(data);

  LayoutInfo info;
  info.set_logical_size(SizeU{1, 2});
  info.set_device_pixel_ratio({2.f, 3.f});

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

  // Updating just one part of the table is enough for it to not be a duplicate.
  LayoutInfo new_info;
  new_info.set_logical_size(SizeU{5, 6});

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
