// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/network_time_service/inspect.h"

#include <lib/async/cpp/executor.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect/testing/cpp/inspect.h>

namespace network_time_service {

class InspectTest : public gtest::TestLoopFixture {
 public:
  InspectTest() : executor_(dispatcher()) {}

 protected:
  void RunPromiseToCompletion(fit::promise<> promise) {
    executor_.schedule_task(std::move(promise));
    RunLoopUntilIdle();
  }

 private:
  async::Executor executor_;
};

TEST_F(InspectTest, Success) {
  inspect::Inspector inspector;
  Inspect inspect(std::move(inspector.GetRoot()));

  RunPromiseToCompletion(
      inspect::ReadFromInspector(inspector).then([&](fit::result<inspect::Hierarchy>& hierarchy) {
        ASSERT_TRUE(hierarchy.is_ok());
        auto* success_count =
            hierarchy.value().node().get_property<inspect::UintPropertyValue>("success_count");
        ASSERT_TRUE(success_count);
        ASSERT_EQ(0u, success_count->value());
      }));

  inspect.Success();
  RunPromiseToCompletion(
      inspect::ReadFromInspector(inspector).then([&](fit::result<inspect::Hierarchy>& hierarchy) {
        ASSERT_TRUE(hierarchy.is_ok());
        auto* success_count =
            hierarchy.value().node().get_property<inspect::UintPropertyValue>("success_count");
        ASSERT_TRUE(success_count);
        ASSERT_EQ(1u, success_count->value());
      }));
}

TEST_F(InspectTest, Failure) {
  inspect::Inspector inspector;
  Inspect inspect(std::move(inspector.GetRoot()));

  RunPromiseToCompletion(
      inspect::ReadFromInspector(inspector).then([&](fit::result<inspect::Hierarchy>& hierarchy) {
        ASSERT_TRUE(hierarchy.is_ok());
        auto* failure_node = hierarchy.value().GetByPath({"failure_count"});
        ASSERT_TRUE(failure_node);
        ASSERT_TRUE(failure_node->children().empty());
      }));

  inspect.Failure(time_server::BAD_RESPONSE);
  RunPromiseToCompletion(
      inspect::ReadFromInspector(inspector).then([&](fit::result<inspect::Hierarchy>& hierarchy) {
        ASSERT_TRUE(hierarchy.is_ok());
        auto* failure_node = hierarchy.value().GetByPath({"failure_count"});
        ASSERT_TRUE(failure_node);
        ASSERT_EQ(1u, failure_node->node().properties().size());
        auto* bad_response =
            failure_node->node().get_property<inspect::UintPropertyValue>("bad_response");
        ASSERT_TRUE(bad_response);
        ASSERT_EQ(1u, bad_response->value());
      }));

  inspect.Failure(time_server::BAD_RESPONSE);
  inspect.Failure(time_server::NETWORK_ERROR);
  RunPromiseToCompletion(
      inspect::ReadFromInspector(inspector).then([&](fit::result<inspect::Hierarchy>& hierarchy) {
        ASSERT_TRUE(hierarchy.is_ok());
        auto* failure_node = hierarchy.value().GetByPath({"failure_count"});
        ASSERT_TRUE(failure_node);
        ASSERT_EQ(2u, failure_node->node().properties().size());
        auto* bad_response =
            failure_node->node().get_property<inspect::UintPropertyValue>("bad_response");
        ASSERT_TRUE(bad_response);
        ASSERT_EQ(2u, bad_response->value());
        auto* network = failure_node->node().get_property<inspect::UintPropertyValue>("network");
        ASSERT_TRUE(network);
        ASSERT_EQ(1u, network->value());
      }));
}

}  // namespace network_time_service
