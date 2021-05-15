// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/hanging_get_helper.h"

#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

using fuchsia::ui::scenic::internal::GraphLinkStatus;
using fuchsia::ui::scenic::internal::LayoutInfo;
using fuchsia::ui::scenic::internal::Vec2;

namespace flatland {
namespace test {

TEST(HangingGetHelperTest, HangingGetProducesValidResponse) {
  async::TestLoop loop;
  HangingGetHelper<Vec2> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_TRUE(helper.HasPendingCallback());
  EXPECT_FALSE(data);

  helper.Update(Vec2{1.0f, 2.0f});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  EXPECT_FALSE(helper.HasPendingCallback());
  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));
}

TEST(HangingGetHelperTest, NonHangingGetProducesValidResponse) {
  async::TestLoop loop;
  HangingGetHelper<Vec2> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  helper.Update(Vec2{1.0f, 2.0f});

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));
}

TEST(HangingGetHelperTest, DataOverrideResultsInFinalValue) {
  async::TestLoop loop;
  HangingGetHelper<Vec2> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  helper.Update(Vec2{1.0f, 2.0f});
  helper.Update(Vec2{3.0f, 4.0f});
  helper.Update(Vec2{5.0f, 6.0f});

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{5.0f, 6.0f}, data.value()));
}

TEST(HangingGetHelperTest, DataOverrideInBatches) {
  async::TestLoop loop;
  HangingGetHelper<Vec2> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(Vec2{1.0f, 2.0f});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));

  helper.Update(Vec2{3.0f, 4.0f});
  helper.Update(Vec2{5.0f, 6.0f});

  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));
  data.reset();
  helper.SetCallback([&](Vec2 d) { data = d; });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{5.0f, 6.0f}, data.value()));
}

TEST(HangingGetHelperTest, DuplicateDataIsIgnored) {
  async::TestLoop loop;
  HangingGetHelper<Vec2> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  std::optional<Vec2> data;
  helper.SetCallback([&](Vec2 d) { data = d; });
  EXPECT_FALSE(data);

  helper.Update(Vec2{1.0f, 2.0f});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{1.0f, 2.0f}, data.value()));

  data.reset();
  helper.SetCallback([&](Vec2 d) { data = d; });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(Vec2{1.0f, 2.0f});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(Vec2{3.0f, 4.0f});

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(Vec2{3.0f, 4.0f}, data.value()));
}

TEST(HangingGetHelperTest, EnumDuplicateDataIsIgnored) {
  async::TestLoop loop;
  HangingGetHelper<GraphLinkStatus> helper(
      std::make_shared<utils::UnownedDispatcherHolder>(loop.dispatcher()));

  std::optional<GraphLinkStatus> data;
  helper.SetCallback([&](GraphLinkStatus d) { data = std::move(d); });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(GraphLinkStatus::CONNECTED_TO_DISPLAY);

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(GraphLinkStatus::CONNECTED_TO_DISPLAY, data.value()));

  data.reset();
  helper.SetCallback([&](GraphLinkStatus d) { data = std::move(d); });

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(GraphLinkStatus::CONNECTED_TO_DISPLAY);

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  loop.RunUntilIdle();

  EXPECT_FALSE(data);

  helper.Update(GraphLinkStatus::DISCONNECTED_FROM_DISPLAY);

  // TODO(fxbug.dev/76183): unnecessary if we use LLCPP bindings (or equivalent).
  EXPECT_FALSE(data);
  loop.RunUntilIdle();

  ASSERT_TRUE(data);
  EXPECT_TRUE(fidl::Equals(GraphLinkStatus::DISCONNECTED_FROM_DISPLAY, data.value()));
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
  info.set_logical_size(Vec2{1.0f, 2.0f});
  info.set_pixel_scale(Vec2{3.f, 4.f});

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
  new_info.set_logical_size(Vec2{5.0f, 6.0f});

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
