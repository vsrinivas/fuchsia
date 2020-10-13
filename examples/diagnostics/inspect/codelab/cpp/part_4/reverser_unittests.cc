// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/cpp/inspect.h>
// [START include_testing]
#include <lib/inspect/testing/cpp/inspect.h>
// [END include_testing]

#include "reverser.h"

using namespace inspect::testing;

using ::testing::AllOf;
using ::testing::UnorderedElementsAre;

class ReverserTest : public gtest::RealLoopFixture {
 public:
  ReverserTest() : executor_(dispatcher()) {}

 protected:
  // Creates a Reverser and return a client Ptr for it.
  fuchsia::examples::inspect::ReverserPtr OpenReverser(ReverserStats stats) {
    fuchsia::examples::inspect::ReverserPtr ptr;

    binding_set_.AddBinding(std::make_unique<Reverser>(std::move(stats)), ptr.NewRequest());

    return ptr;
  }

  // Run a promise to completion on the default async executor.
  void RunPromiseToCompletion(fit::promise<> promise) {
    bool done = false;
    executor_.schedule_task(std::move(promise).and_then([&]() { done = true; }));
    RunLoopUntil([&] { return done; });
  }

  // Get the number of active connections.
  //
  // This allows us to wait until a connection closes.
  size_t connection_count() const { return binding_set_.size(); }

 private:
  async::Executor executor_;
  fidl::BindingSet<fuchsia::examples::inspect::Reverser, std::unique_ptr<Reverser>> binding_set_;
};

TEST_F(ReverserTest, ReversePart4) {
  inspect::Inspector inspector;

  auto global_requests =
      std::make_shared<inspect::UintProperty>(inspector.GetRoot().CreateUint("request_count", 0));

  auto ptr1 = OpenReverser({.connection_node = inspector.GetRoot().CreateChild("connection_0x0"),
                            .global_request_count = global_requests});

  auto ptr2 = OpenReverser({.connection_node = inspector.GetRoot().CreateChild("connection_0x1"),
                            .global_request_count = global_requests});

  {
    bool done = false;
    std::string value;
    ptr1->Reverse("hello", [&](std::string response) {
      value = std::move(response);
      done = true;
    });
    RunLoopUntil([&] { return done; });
    EXPECT_EQ("olleh", value);
  }

  {
    bool done = false;
    std::string value;
    ptr1->Reverse("world", [&](std::string response) {
      value = std::move(response);
      done = true;
    });
    RunLoopUntil([&] { return done; });
    EXPECT_EQ("dlrow", value);
  }

  {
    bool done = false;
    std::string value;
    ptr2->Reverse("another", [&](std::string response) {
      value = std::move(response);
      done = true;
    });
    RunLoopUntil([&] { return done; });
    EXPECT_EQ("rehtona", value);
  }

  {
    // [START get_hierarchy]
    fit::result<inspect::Hierarchy> hierarchy;
    RunPromiseToCompletion(inspect::ReadFromInspector(inspector).then(
        [&](fit::result<inspect::Hierarchy>& result) { hierarchy = std::move(result); }));
    ASSERT_TRUE(hierarchy.is_ok());
    // [END get_hierarchy]

    // [START assertions]
    auto* global_count =
        hierarchy.value().node().get_property<inspect::UintPropertyValue>("request_count");
    ASSERT_TRUE(global_count);
    EXPECT_EQ(3u, global_count->value());

    auto* connection_0 = hierarchy.value().GetByPath({"connection_0x0"});
    ASSERT_TRUE(connection_0);
    auto* requests_0 =
        connection_0->node().get_property<inspect::UintPropertyValue>("request_count");
    ASSERT_TRUE(requests_0);
    EXPECT_EQ(2u, requests_0->value());
    // [END assertions]

    auto* connection_1 = hierarchy.value().GetByPath({"connection_0x1"});
    ASSERT_TRUE(connection_1);
    auto* requests_1 =
        connection_1->node().get_property<inspect::UintPropertyValue>("request_count");
    ASSERT_TRUE(requests_1);
    EXPECT_EQ(1u, requests_1->value());
  }

  // Unbind the first pointer. We should no longer see the connection_0x0.
  ptr1.Unbind();
  RunLoopUntil([&] { return connection_count() == 1; });

  {
    fit::result<inspect::Hierarchy> hierarchy;
    RunPromiseToCompletion(inspect::ReadFromInspector(inspector).then(
        [&](fit::result<inspect::Hierarchy>& result) { hierarchy = std::move(result); }));
    ASSERT_TRUE(hierarchy.is_ok());

    auto* connection_0 = hierarchy.value().GetByPath({"connection_0x0"});
    ASSERT_FALSE(connection_0);

    auto* connection_1 = hierarchy.value().GetByPath({"connection_0x1"});
    ASSERT_TRUE(connection_1);
  }
}
