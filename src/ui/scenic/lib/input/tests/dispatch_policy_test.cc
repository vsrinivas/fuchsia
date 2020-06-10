// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/input/tests/util.h"

// These tests exercise input event delivery under different dispatch policies.
// Setup:
// - Injection done in context View Space, with fuchsia.ui.pointerinjector
// - Target(s) specified by View (using view ref koids)
// - Dispatch done in fuchsia.ui.scenic.SessionListener (legacy)

namespace lib_ui_input_tests {
namespace {

using fuchsia::ui::input::PointerEventPhase;

struct TestScene {
  SessionWrapper root_session;
  ResourceGraph root_resources;
  SessionWrapper client_session1;
  SessionWrapper client_session2;
  SessionWrapper client_session3;
  SessionWrapper client_session4;
};

// Sets up a 9x9 "display".
class DispatchPolicyTest : public InputSystemTest {
 protected:
  static constexpr fuchsia::ui::gfx::ViewProperties k5x5x1000 = {
      .bounding_box = {.min = {0, 0, -1000}, .max = {5, 5, 0}}};

  // Creates a Scene Graph with layout:
  // Root
  //   |
  // View1
  //   |
  // View2
  //   |  \
  // View4 View3
  //
  // Scene Graph layout:
  // All views are exactly overlapping. Each view sets up an identical rectangle,
  // but at different z positions.
  // Z ordering of rectangles:
  // -----View4 Rect----
  // -----View3 Rect----
  // -----View2 Rect----
  // -----View1 Rect----
  //
  TestScene CreateTestScene() {
    auto [v1, vh1] = scenic::ViewTokenPair::New();
    auto [v2, vh2] = scenic::ViewTokenPair::New();
    auto [v3, vh3] = scenic::ViewTokenPair::New();
    auto [v4, vh4] = scenic::ViewTokenPair::New();

    // Set up a scene with two ViewHolders, one a child of the other.
    auto [root_session, root_resources] = CreateScene();
    scenic::ViewHolder holder_1(root_session.session(), std::move(vh1), "holder_1");
    {
      holder_1.SetViewProperties(k5x5x1000);
      root_resources.scene.AddChild(holder_1);
      RequestToPresent(root_session.session());
    }

    SessionWrapper client_1 = CreateClient("view_1", std::move(v1));
    {
      scenic::ViewHolder holder_2(client_1.session(), std::move(vh2), "holder_2");
      holder_2.SetViewProperties(k5x5x1000);
      client_1.view()->AddChild(holder_2);

      scenic::ShapeNode shape(client_1.session());
      shape.SetTranslation(2.5f, 2.5f, 0);  // Center the shape within the View.
      client_1.view()->AddChild(shape);
      scenic::Rectangle rec(client_1.session(), 5, 5);  // Size of the view.
      shape.SetShape(rec);

      RequestToPresent(client_1.session());
    }

    SessionWrapper client_2 = CreateClient("view_2", std::move(v2));
    {
      scenic::ViewHolder holder_3(client_2.session(), std::move(vh3), "holder_3");
      holder_3.SetViewProperties(k5x5x1000);
      client_2.view()->AddChild(holder_3);

      scenic::ViewHolder holder_4(client_2.session(), std::move(vh4), "holder_4");
      holder_4.SetViewProperties(k5x5x1000);
      client_2.view()->AddChild(holder_4);

      scenic::ShapeNode shape(client_2.session());
      shape.SetTranslation(2.5f, 2.5f, -1);  // Center the shape within the View.
      client_2.view()->AddChild(shape);
      scenic::Rectangle rec(client_2.session(), 5, 5);  // Size of the view.
      shape.SetShape(rec);

      RequestToPresent(client_2.session());
    }

    SessionWrapper client_3 = CreateClient("view_3", std::move(v3));
    {
      scenic::ShapeNode shape(client_3.session());
      shape.SetTranslation(2.5f, 2.5f, -2);  // Center the shape within the View.
      client_3.view()->AddChild(shape);
      scenic::Rectangle rec(client_3.session(), 5, 5);  // Size of the view.
      shape.SetShape(rec);

      RequestToPresent(client_3.session());
    }

    SessionWrapper client_4 = CreateClient("view_4", std::move(v4));
    {
      scenic::ShapeNode shape(client_4.session());
      shape.SetTranslation(2.5f, 2.5f, -3);  // Center the shape within the View.
      client_4.view()->AddChild(shape);
      scenic::Rectangle rec(client_4.session(), 5, 5);  // Size of the view.
      shape.SetShape(rec);

      RequestToPresent(client_4.session());
    }

    return {
        .root_session = std::move(root_session),
        .root_resources = std::move(root_resources),
        .client_session1 = std::move(client_1),
        .client_session2 = std::move(client_2),
        .client_session3 = std::move(client_3),
        .client_session4 = std::move(client_4),
    };
  }

  uint32_t test_display_width_px() const override { return 9; }
  uint32_t test_display_height_px() const override { return 9; }
};

TEST_F(DispatchPolicyTest, ExclusiveMode_ShouldOnlyDeliverToTarget) {
  TestScene test_scene = CreateTestScene();

  // Scene is set up. Inject and check output.
  {
    RegisterInjector(
        /*context=*/test_scene.client_session1.view_ref(),
        /*target=*/test_scene.client_session2.view_ref(),
        fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET,
        fuchsia::ui::pointerinjector::DeviceType::TOUCH,
        /*extents*/
        {{/*min*/ {0.f, 0.f}, /*max*/ {static_cast<float>(test_display_width_px()),
                                       static_cast<float>(test_display_height_px())}}});
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::ADD);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  {  // Target should receive events.
    const std::vector<fuchsia::ui::input::InputEvent>& events = test_scene.client_session2.events();
    ASSERT_EQ(events.size(), 5u);
    EXPECT_EQ(events[0].pointer().phase, PointerEventPhase::ADD);
    EXPECT_EQ(events[1].pointer().phase, PointerEventPhase::DOWN);
    EXPECT_EQ(events[2].pointer().phase, PointerEventPhase::MOVE);
    EXPECT_EQ(events[3].pointer().phase, PointerEventPhase::UP);
    EXPECT_EQ(events[4].pointer().phase, PointerEventPhase::REMOVE);
  }

  {  // No other client should receive any events.
    EXPECT_TRUE(test_scene.client_session1.events().empty());
    EXPECT_TRUE(test_scene.client_session3.events().empty());
    EXPECT_TRUE(test_scene.client_session4.events().empty());
  }
}

TEST_F(DispatchPolicyTest, TopHitMode_OnLeafTarget_ShouldOnlyDeliverToTopHit) {
  TestScene test_scene = CreateTestScene();

  // Inject with View3 as target. Top hit should be View3.
  {
    RegisterInjector(/*context=*/test_scene.client_session1.view_ref(),
                     /*target=*/test_scene.client_session3.view_ref(),
                     fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET,
                     fuchsia::ui::pointerinjector::DeviceType::TOUCH,
                     /*extents*/
                     {{/*min*/ {0.f, 0.f},
                       /*max*/ {static_cast<float>(test_display_width_px()),
                                static_cast<float>(test_display_height_px())}}});
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::ADD);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  {  // Target should receive events.
    const std::vector<fuchsia::ui::input::InputEvent>& events = test_scene.client_session3.events();
    ASSERT_EQ(events.size(), 6u);
    EXPECT_EQ(events[0].pointer().phase, PointerEventPhase::ADD);
    EXPECT_TRUE(events[1].is_focus());
    EXPECT_EQ(events[2].pointer().phase, PointerEventPhase::DOWN);
    EXPECT_EQ(events[3].pointer().phase, PointerEventPhase::MOVE);
    EXPECT_EQ(events[4].pointer().phase, PointerEventPhase::UP);
    EXPECT_EQ(events[5].pointer().phase, PointerEventPhase::REMOVE);
  }

  {  // No other client should receive any events.
    EXPECT_TRUE(test_scene.client_session1.events().empty());
    EXPECT_TRUE(test_scene.client_session2.events().empty());
    EXPECT_TRUE(test_scene.client_session4.events().empty());
  }
}

TEST_F(DispatchPolicyTest, TopHitMode_OnMidTreeTarget_ShouldOnlyDeliverToTopHit) {
  TestScene test_scene = CreateTestScene();

  // Inject with View2 as target. Top hit should be View4.
  {
    RegisterInjector(/*context=*/test_scene.client_session1.view_ref(),
                     /*target=*/test_scene.client_session2.view_ref(),
                     fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET,
                     fuchsia::ui::pointerinjector::DeviceType::TOUCH,
                     /*extents*/
                     {{/*min*/ {0.f, 0.f},
                       /*max*/ {static_cast<float>(test_display_width_px()),
                                static_cast<float>(test_display_height_px())}}});
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::ADD);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  {  // Target should receive events.
    const std::vector<fuchsia::ui::input::InputEvent>& events = test_scene.client_session4.events();
    ASSERT_EQ(events.size(), 6u);
    EXPECT_EQ(events[0].pointer().phase, PointerEventPhase::ADD);
    EXPECT_TRUE(events[1].is_focus());
    EXPECT_EQ(events[2].pointer().phase, PointerEventPhase::DOWN);
    EXPECT_EQ(events[3].pointer().phase, PointerEventPhase::MOVE);
    EXPECT_EQ(events[4].pointer().phase, PointerEventPhase::UP);
    EXPECT_EQ(events[5].pointer().phase, PointerEventPhase::REMOVE);
  }

  {  // No other client should receive any events.
    EXPECT_TRUE(test_scene.client_session1.events().empty());
    EXPECT_TRUE(test_scene.client_session2.events().empty());
    EXPECT_TRUE(test_scene.client_session3.events().empty());
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
