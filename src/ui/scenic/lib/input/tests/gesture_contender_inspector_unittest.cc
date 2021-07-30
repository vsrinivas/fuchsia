// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/gesture_contender_inspector.h"

#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace input::test {

constexpr zx_koid_t kViewRefKoid = 1;

class GestureContenderInspectorTest : public gtest::TestLoopFixture {
 protected:
  struct Values {
    uint64_t num_injected_events = 0;
    uint64_t num_won_streams = 0;
    uint64_t num_lost_streams = 0;
  };

  void SetUp() override {
    gesture_contender_inspector_.emplace(inspector_.GetRoot().CreateChild("inspector"));
  }

  std::optional<Values> GetValuesAtMinute(uint64_t minute, zx_koid_t view_ref_koid) {
    const auto [root, parent] = ReadHierarchyFromInspector();
    const inspect::Hierarchy* top_node = parent->GetByPath({kHistoryNodeName});
    FX_CHECK(top_node);

    const inspect::Hierarchy* minute_node =
        top_node->GetByPath({"Events at minute " + std::to_string(minute)});
    if (!minute_node) {
      FX_LOGS(INFO) << "Found no data for minute " << minute;
      return std::nullopt;
    }

    const inspect::Hierarchy* view_node =
        minute_node->GetByPath({"View " + std::to_string(view_ref_koid)});
    if (!view_node) {
      FX_LOGS(INFO) << "Found no data for view " << view_ref_koid;
      return std::nullopt;
    }

    return ValuesFromNode(view_node->node());
  }

  Values GetSumOfAllMinutes() {
    const auto [root, parent] = ReadHierarchyFromInspector();
    const inspect::Hierarchy* top_node = parent->GetByPath({kHistoryNodeName});
    FX_CHECK(top_node);
    const inspect::Hierarchy* sum_node = top_node->GetByPath({"Sum"});
    FX_CHECK(sum_node);
    return ValuesFromNode(sum_node->node());
  }

  std::optional<scenic_impl::input::GestureContenderInspector> gesture_contender_inspector_;

 private:
  Values ValuesFromNode(const inspect::NodeValue& node) const {
    return {
        .num_injected_events =
            node.get_property<inspect::UintPropertyValue>("num_injected_events")->value(),
        .num_won_streams =
            node.get_property<inspect::UintPropertyValue>("num_won_streams")->value(),
        .num_lost_streams =
            node.get_property<inspect::UintPropertyValue>("num_lost_streams")->value(),
    };
  }

  std::pair<inspect::Hierarchy, const inspect::Hierarchy*> ReadHierarchyFromInspector() {
    fpromise::result<inspect::Hierarchy> result;
    fpromise::single_threaded_executor exec;
    exec.schedule_task(
        inspect::ReadFromInspector(inspector_).then([&](fpromise::result<inspect::Hierarchy>& res) {
          result = std::move(res);
        }));
    exec.run();

    inspect::Hierarchy root = result.take_value();
    const inspect::Hierarchy* hierarchy = root.GetByPath({"inspector"});
    FX_CHECK(hierarchy);
    return {std::move(root), hierarchy};
  }

  const std::string kHistoryNodeName =
      "Last " +
      std::to_string(scenic_impl::input::GestureContenderInspector::kNumMinutesOfHistory) +
      " minutes of injected events";
  inspect::Inspector inspector_;
};

TEST_F(GestureContenderInspectorTest, InspectHistory) {
  const uint64_t kMax = scenic_impl::input::GestureContenderInspector::kNumMinutesOfHistory;
  const uint64_t start_minute = Now().get() / zx::min(1).get();

  {
    EXPECT_FALSE(GetValuesAtMinute(start_minute, kViewRefKoid));
    auto values = GetSumOfAllMinutes();
    EXPECT_EQ(values.num_injected_events, 0u);
    EXPECT_EQ(values.num_won_streams, 0u);
    EXPECT_EQ(values.num_lost_streams, 0u);
  }

  gesture_contender_inspector_->OnInjectedEvents(kViewRefKoid, 1);
  {
    auto values = GetValuesAtMinute(start_minute, kViewRefKoid);
    ASSERT_TRUE(values.has_value());
    EXPECT_EQ(values->num_injected_events, 1u);
    EXPECT_EQ(values->num_won_streams, 0u);
    EXPECT_EQ(values->num_lost_streams, 0u);
  }
  {
    auto values = GetSumOfAllMinutes();
    EXPECT_EQ(values.num_injected_events, 1u);
    EXPECT_EQ(values.num_won_streams, 0u);
    EXPECT_EQ(values.num_lost_streams, 0u);
  }

  // Calling multiple times during the same minute should add to the previous value.
  gesture_contender_inspector_->OnInjectedEvents(kViewRefKoid, 2);
  gesture_contender_inspector_->OnContestDecided(kViewRefKoid, /*won=*/true);  // Check won contest.
  gesture_contender_inspector_->OnContestDecided(kViewRefKoid, /*won=*/true);
  gesture_contender_inspector_->OnContestDecided(kViewRefKoid, /*won=*/true);
  gesture_contender_inspector_->OnContestDecided(kViewRefKoid,
                                                 /*won=*/false);  // Check a lost contest.
  {
    auto values = GetValuesAtMinute(start_minute, kViewRefKoid);
    ASSERT_TRUE(values.has_value());
    EXPECT_EQ(values->num_injected_events, 3u);
    EXPECT_EQ(values->num_won_streams, 3u);
    EXPECT_EQ(values->num_lost_streams, 1u);
  }
  {
    auto values = GetSumOfAllMinutes();
    EXPECT_EQ(values.num_injected_events, 3u);
    EXPECT_EQ(values.num_won_streams, 3u);
    EXPECT_EQ(values.num_lost_streams, 1u);
  }

  // Wait one minute and add more data.
  RunLoopFor(zx::min(1));
  gesture_contender_inspector_->OnInjectedEvents(kViewRefKoid, 5);
  gesture_contender_inspector_->OnContestDecided(kViewRefKoid, /*won=*/true);
  gesture_contender_inspector_->OnContestDecided(kViewRefKoid, /*won=*/false);
  {
    auto values = GetValuesAtMinute(start_minute, kViewRefKoid);
    ASSERT_TRUE(values.has_value());
    EXPECT_EQ(values->num_injected_events, 3u);
    EXPECT_EQ(values->num_won_streams, 3u);
    EXPECT_EQ(values->num_lost_streams, 1u);
  }
  {
    auto values = GetValuesAtMinute(start_minute + 1u, kViewRefKoid);
    ASSERT_TRUE(values.has_value());
    EXPECT_EQ(values->num_injected_events, 5u);
    EXPECT_EQ(values->num_won_streams, 1u);
    EXPECT_EQ(values->num_lost_streams, 1u);
  }
  {
    auto values = GetSumOfAllMinutes();
    EXPECT_EQ(values.num_injected_events, 8u);
    EXPECT_EQ(values.num_won_streams, 4u);
    EXPECT_EQ(values.num_lost_streams, 2u);
  }

  // Wait until the first minute should have dropped out.
  RunLoopFor(zx::min(kMax - 1));
  EXPECT_FALSE(GetValuesAtMinute(start_minute, kViewRefKoid));
  {
    auto values = GetValuesAtMinute(start_minute + 1u, kViewRefKoid);
    ASSERT_TRUE(values.has_value());
    EXPECT_EQ(values->num_injected_events, 5u);
    EXPECT_EQ(values->num_won_streams, 1u);
    EXPECT_EQ(values->num_lost_streams, 1u);
  }
  {
    auto values = GetSumOfAllMinutes();
    EXPECT_EQ(values.num_injected_events, 5u);
    EXPECT_EQ(values.num_won_streams, 1u);
    EXPECT_EQ(values.num_lost_streams, 1u);
  }

  // And check that we correctly track the next minute.
  gesture_contender_inspector_->OnInjectedEvents(kViewRefKoid, 25);
  {
    auto values = GetValuesAtMinute(start_minute + kMax, kViewRefKoid);
    ASSERT_TRUE(values.has_value());
    EXPECT_EQ(values->num_injected_events, 25u);
    EXPECT_EQ(values->num_won_streams, 0u);
    EXPECT_EQ(values->num_lost_streams, 0u);
  }
  {
    auto values = GetSumOfAllMinutes();
    EXPECT_EQ(values.num_injected_events, 30u);
    EXPECT_EQ(values.num_won_streams, 1u);
    EXPECT_EQ(values.num_lost_streams, 1u);
  }
}

}  // namespace input::test
