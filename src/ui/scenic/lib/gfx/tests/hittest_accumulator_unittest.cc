// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/ui/scenic/lib/gfx/engine/hit.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl {
namespace gfx {
namespace test {
namespace {

using namespace testing;

// Empty accumulator should report no collisions.
TEST(CollisionAccumulatorTest, Empty) { EXPECT_TRUE(CollisionAccumulator().Report().empty()); }

std::vector<fxl::RefPtr<ShapeNode>> SetUpFakeNodes() {
  std::vector<fxl::RefPtr<ShapeNode>> nodes;
  static constexpr size_t num_nodes = 6;
  nodes.reserve(num_nodes);
  for (size_t i = 0; i < num_nodes; ++i) {
    nodes.push_back(fxl::MakeRefCounted<ShapeNode>(/*session=*/nullptr,
                                                   /*session_id=*/num_nodes - i,
                                                   /*node_id=*/i));
  }
  return nodes;
}

// Nodes at same distance should collide.
TEST(CollisionAccumulatorTest, Warning) {
  auto nodes = SetUpFakeNodes();

  CollisionAccumulator accumulator;

  static constexpr float distance1 = 100.0f, distance2 = 200.0f, distance3 = 300.0f;
  accumulator.Add({.node = nodes[0].get(), .distance = distance1});
  accumulator.Add({.node = nodes[1].get(), .distance = distance1});
  accumulator.Add({.node = nodes[2].get(), .distance = distance1});
  accumulator.Add({.node = nodes[3].get(), .distance = distance2});
  accumulator.Add({.node = nodes[4].get(), .distance = distance3});
  accumulator.Add({.node = nodes[5].get(), .distance = distance3});

  EXPECT_THAT(
      accumulator.Report(),
      UnorderedElementsAre(UnorderedElementsAre(GlobalId{6, 0}, GlobalId{5, 1}, GlobalId{4, 2}),
                           UnorderedElementsAre(GlobalId{2, 4}, GlobalId{1, 5})));
}

// Nodes at different distances should have no collisions.
TEST(CollisionAccumulatorTest, NoCollisions) {
  const auto nodes = SetUpFakeNodes();

  CollisionAccumulator accumulator;
  for (size_t i = 0; i < nodes.size(); ++i) {
    accumulator.Add({.node = nodes[i].get(), .distance = 100 + i * 1.0f});
  }

  EXPECT_TRUE(accumulator.Report().empty());
}

class SessionHitAccumulatorTest : public SessionTest {
 protected:
  ViewPtr CreateView(Session* session, ResourceId view_id) {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    CommandContext command_context;
    session->ApplyCommand(&command_context,
                          scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
    command_context.Flush();
    return session->resources()->FindResource<View>(view_id);
  }

 private:
  // |SessionTest|
  SessionContext CreateSessionContext() override {
    SessionContext session_context = SessionTest::CreateSessionContext();
    session_context.view_linker = &view_linker_;
    return session_context;
  }

  ViewLinker view_linker_;
};

TEST_F(SessionHitAccumulatorTest, Empty) {
  SessionHitAccumulator accumulator;
  accumulator.EndLayer();
  EXPECT_TRUE(accumulator.hits().empty());
}

TEST_F(SessionHitAccumulatorTest, TopHitInASession) {
  SessionHitAccumulator accumulator;
  ViewPtr view = CreateView(session(), 1);

  accumulator.Add({.view = view, .distance = 2});
  accumulator.Add({.view = view, .distance = 1});
  accumulator.Add({.view = view, .distance = 3});
  accumulator.EndLayer();

  EXPECT_THAT(accumulator.hits(), ElementsAre(Field(&ViewHit::distance, 1)));
}

MATCHER(ViewIdEq, "view ID equals") {
  const auto& [hit, n] = arg;
  return hit.view->id() == n;
}

TEST_F(SessionHitAccumulatorTest, SortedHitsPerLayer) {
  std::unique_ptr<Session> s1 = CreateSession(), s2 = CreateSession(), s3 = CreateSession();
  // Views must go out of scope before their sessions, and accumulator holds onto views.
  ViewPtr v1 = CreateView(s1.get(), 1), v2 = CreateView(s2.get(), 2), v3 = CreateView(s3.get(), 3);
  SessionHitAccumulator accumulator;

  // Add hits in two layers to make sure we sort each one independently.

  accumulator.Add({.view = v1, .distance = 2});
  accumulator.Add({.view = v2, .distance = 1});
  accumulator.Add({.view = v3, .distance = 3});
  accumulator.EndLayer();

  accumulator.Add({.view = v1, .distance = 2});
  accumulator.Add({.view = v2, .distance = 3});
  accumulator.Add({.view = v3, .distance = 1});
  accumulator.EndLayer();

  EXPECT_THAT(accumulator.hits(), testing::Pointwise(ViewIdEq(), {2u, 1u, 3u, 3u, 1u, 2u}));
}

TEST(TopHitAccumulatorTest, Empty) {
  TopHitAccumulator accumulator;
  EXPECT_TRUE(accumulator.EndLayer()) << "Hit testing should continue until a hit is found.";
  EXPECT_FALSE(accumulator.hit());
}

TEST(TopHitAccumulatorTest, LayersStopAfterHit) {
  TopHitAccumulator accumulator;
  accumulator.Add({});
  EXPECT_FALSE(accumulator.EndLayer());
}

TEST(TopHitAccumulatorTest, TopHit) {
  TopHitAccumulator accumulator;
  accumulator.Add({.distance = 2});
  accumulator.Add({.distance = 1});
  accumulator.Add({.distance = 3});
  EXPECT_THAT(accumulator.hit(), Optional(Field(&ViewHit::distance, 1)));
}

}  // namespace
}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
