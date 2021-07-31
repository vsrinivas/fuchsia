// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/hanging_get_helper.h"

#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

using fuchsia::math::SizeU;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::ParentViewportStatus;

namespace flatland {
namespace test {

TEST(HangingGetHelperTest, HangingGetProducesValidResponse) {
  async::TestLoop loop;
  HangingGetHelper<SizeU> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });
  EXPECT_TRUE(helper.HasPendingCallback());
  EXPECT_FALSE(data);

  helper.Update(SizeU{1, 2});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  EXPECT_FALSE(helper.HasPendingCallback());
  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));
}

TEST(HangingGetHelperTest, NonHangingGetProducesValidResponse) {
  async::TestLoop loop;
  HangingGetHelper<SizeU> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  helper.Update(SizeU{1, 2});

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));
}

TEST(HangingGetHelperTest, DataOverrideResultsInFinalValue) {
  async::TestLoop loop;
  HangingGetHelper<SizeU> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  helper.Update(SizeU{1, 2});
  helper.Update(SizeU{3, 4});
  helper.Update(SizeU{5, 6});

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{5, 6}, data.value()));
}

TEST(HangingGetHelperTest, DataOverrideInBatches) {
  async::TestLoop loop;
  HangingGetHelper<SizeU> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(SizeU{1, 2});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));

  helper.Update(SizeU{3, 4});
  helper.Update(SizeU{5, 6});

  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));
  data.reset();
  helper.SetCallback([&](SizeU d) { data = d; });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{5, 6}, data.value()));
}

TEST(HangingGetHelperTest, DuplicateDataIsIgnored) {
  async::TestLoop loop;
  HangingGetHelper<SizeU> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  std::optional<SizeU> data;
  helper.SetCallback([&](SizeU d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(SizeU{1, 2});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{1, 2}, data.value()));

  data.reset();
  helper.SetCallback([&](SizeU d) { data = d; });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(SizeU{1, 2});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(SizeU{3, 4});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(SizeU{3, 4}, data.value()));
}

TEST(HangingGetHelperTest, EnumDuplicateDataIsIgnored) {
  async::TestLoop loop;
  HangingGetHelper<ParentViewportStatus> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  std::optional<ParentViewportStatus> data;
  helper.SetCallback([&](ParentViewportStatus d) { data = std::move(d); });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(ParentViewportStatus::CONNECTED_TO_DISPLAY);

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(ParentViewportStatus::CONNECTED_TO_DISPLAY, data.value()));

  data.reset();
  helper.SetCallback([&](ParentViewportStatus d) { data = std::move(d); });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(ParentViewportStatus::CONNECTED_TO_DISPLAY);

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(ParentViewportStatus::DISCONNECTED_FROM_DISPLAY);

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(ParentViewportStatus::DISCONNECTED_FROM_DISPLAY, data.value()));
}

TEST(HangingGetHelperTest, TableDuplicateDataIsIgnored) {
  async::TestLoop loop;
  HangingGetHelper<LayoutInfo> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));
  std::optional<LayoutInfo> data;
  helper.SetCallback([&](LayoutInfo d) { data = std::move(d); });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  LayoutInfo info;
  info.set_logical_size(SizeU{1, 2});
  info.set_pixel_scale(SizeU{3, 4});

  {
    LayoutInfo info2;
    info.Clone(&info2);
    helper.Update(std::move(info2));
  }

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(info, data.value()));

  data.reset();
  helper.SetCallback([&](LayoutInfo d) { data = std::move(d); });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  {
    LayoutInfo info3;
    info.Clone(&info3);
    helper.Update(std::move(info3));
  }

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  // Updating just one part of the table is enough for it to not be a duplicate.
  LayoutInfo new_info;
  new_info.set_logical_size(SizeU{5, 6});

  {
    LayoutInfo info4;
    new_info.Clone(&info4);
    helper.Update(std::move(info4));
  }

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(new_info, data.value()));
}

}  // namespace test
}  // namespace flatland
