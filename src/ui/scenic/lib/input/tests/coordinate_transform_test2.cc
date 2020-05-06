// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/input/tests/util.h"

// These tests exercise the context View Space to target View Space coordinate transform logic
// applied to pointer events sent to sessions using the input injection API.
// Setup:
// - Injection done in context View Space, with fuchsia.ui.pointerflow
// - Target(s) specified by View (using view ref koids)
// - Dispatch done in fuchsia.ui.scenic.SessionListener (legacy)

namespace lib_ui_input_tests {
namespace {

using fuchsia::ui::input::PointerEventPhase;

// Sets up a 9x9 "display".
class CoordinateTransformTest2 : public InputSystemTest {
 protected:
  void SetUp() override { InputSystemTest::SetUp(); }

  void TearDown() override {
    injector_ = {};
    InputSystemTest::TearDown();
  }

  void Inject(float x, float y, fuchsia::ui::input3::PointerEventPhase phase) {
    FX_CHECK(injector_);
    fuchsia::ui::pointerflow::Event event;
    event.set_timestamp(0);
    event.set_pointer_id(1);
    event.set_phase(phase);
    event.set_position_x(x);
    event.set_position_y(y);
    std::vector<fuchsia::ui::pointerflow::Event> events;
    events.emplace_back(std::move(event));
    injector_->Inject(std::move(events), [] {});
  }

  void RegisterInjector(fuchsia::ui::views::ViewRef context_view_ref,
                        fuchsia::ui::views::ViewRef target_view_ref) {
    fuchsia::ui::pointerflow::InjectorConfig config;
    {
      {
        fuchsia::ui::pointerflow::DeviceConfig device_config;
        device_config.set_device_id(1);
        device_config.set_device_type(fuchsia::ui::input3::PointerDeviceType::TOUCH);
        config.set_device_config(std::move(device_config));
      }
      {
        fuchsia::ui::pointerflow::InjectorContext context;
        context.set_view(std::move(context_view_ref));
        config.set_context(std::move(context));
      }
      {
        fuchsia::ui::pointerflow::InjectorTarget target;
        target.set_view(std::move(target_view_ref));
        config.set_target(std::move(target));
      }
      config.set_dispatch_policy(fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE);
    }

    bool error_callback_fired = false;
    injector_.set_error_handler([&error_callback_fired](zx_status_t) {
      FX_LOGS(ERROR) << "Channel closed.";
      error_callback_fired = true;
    });
    bool register_callback_fired = false;
    input_system()->Register(std::move(config), injector_.NewRequest(),
                             [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntilIdle();
    ASSERT_TRUE(register_callback_fired);
    ASSERT_FALSE(error_callback_fired);
  }

  uint32_t test_display_width_px() const override { return 9; }
  uint32_t test_display_height_px() const override { return 9; }

 private:
  fuchsia::ui::pointerflow::InjectorPtr injector_;
};

// In this test we set up the context and the target. We apply a scale, rotation and translation
// transform to both of their view holder nodes, and then inject pointer events to confirm that
// the coordinates received by the listener are correctly transformed.
// Only the transformation of the target, relative to the context, should have any effect on
// the output.
//
// Below are ASCII diagrams showing the transformation *difference* between target and context.
// Note that the dashes represent the context view and notated X,Y coordinate system is the
// context's coordinate system. The target view's coordinate system has its origin at corner '1'.
//
// Scene pre-transformation (1,2,3,4 denote the corners of the target view):
//   X ->
// Y 1 O O O O 2
// | O O O O O O
// v O O O O O O
//   O O O O O O
//   O O O O O O
//   4 O O O O 3
//
// After scale:
//   X ->
// Y 1 - O - O - O   O   2
// | - - - - - - -
// V - - - - - - -
//   O - O - O - O   O   O
//   - - - - - - -
//   - - - - - - -
//   O   O   O   O   O   O
//
//
//   O   O   O   O   O   O
//
//
//   O   O   O   O   O   O
//
//
//   4   O   O   O   O   3
//
// After rotation:
//   X ->
// Y 4      O      O      O      O      1 - - - - - -
// |                                      - - - - - -
// V O      O      O      O      O      O - - - - - -
//                                        - - - - - -
//   O      O      O      O      O      O - - - - - -
//                                        - - - - - -
//   O      O      O      O      O      O
//
//   O      O      O      O      O      O
//
//   3      O      O      O      O      2
//
// After translation:
//   X ->
// Y 4      O      O      O      O    D 1 - - - M1
// |                                  - - - - - -
// V O      O      O      O      O    - O - - - -
//                                    - - - - - -
//   O      O      O      O      O    - O - - - -
//                                    U - - - - M2
//   O      O      O      O      O      O
//
//   O      O      O      O      O      O
//
//   3      O      O      O      O      2
//
TEST_F(CoordinateTransformTest2, InjectedInput_ShouldBeCorrectlyTransformed) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_session, root_resources] = CreateScene();
  scenic::ViewHolder holder_1(root_session.session(), std::move(vh1), "holder_1");
  {
    holder_1.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(holder_1);
    // Scale, rotate and translate the context to verify that it has no effect on the outcome.
    holder_1.SetScale(2, 3, 1);
    const auto rotation_quaternion = glm::angleAxis(glm::pi<float>() / 2.f, glm::vec3(0, 0, 1));
    holder_1.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                         rotation_quaternion.w);
    holder_1.SetTranslation(1, 0, 0);

    RequestToPresent(root_session.session());
  }

  SessionWrapper client_1 = CreateClient("view_1", std::move(v1));
  scenic::ViewHolder holder_2(client_1.session(), std::move(vh2), "holder_2");
  {
    holder_2.SetViewProperties(k5x5x1);

    client_1.view()->AddChild(holder_2);

    // Scale, rotate and translate target.
    // Scale X by 2 and Y by 3.
    holder_2.SetScale(2, 3, 1);
    // Rotate 90 degrees counter clockwise around Z-axis (Z-axis points into screen, so appears as
    // clockwise rotation).
    const auto rotation_quaternion = glm::angleAxis(glm::pi<float>() / 2.f, glm::vec3(0, 0, 1));
    holder_2.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                         rotation_quaternion.w);
    // Translate by 1 in the X direction.
    holder_2.SetTranslation(1, 0, 0);

    RequestToPresent(client_1.session());
  }

  SessionWrapper client_2 = CreateClient("view_2", std::move(v2));

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.
  {
    RegisterInjector(client_1.view_ref(), client_2.view_ref());
    Inject(0, 0, fuchsia::ui::input3::PointerEventPhase::ADD);
    Inject(5, 0, fuchsia::ui::input3::PointerEventPhase::CHANGE);
    Inject(5, 5, fuchsia::ui::input3::PointerEventPhase::CHANGE);
    Inject(0, 5, fuchsia::ui::input3::PointerEventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  {
    // Context should receive no events.
    const std::vector<fuchsia::ui::input::InputEvent>& events = client_1.events();
    EXPECT_EQ(events.size(), 0u);
  }

  {  // Target should receive events correctly transformed to its Local Space.
    const std::vector<fuchsia::ui::input::InputEvent>& events = client_2.events();
    ASSERT_EQ(events.size(), 6u);

    // Targets gets properly transformed input coordinates.
    EXPECT_TRUE(
        PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 0.0 / 2.0, 1.0 / 3.0));
    EXPECT_TRUE(
        PointerMatches(events[1].pointer(), 1u, PointerEventPhase::DOWN, 0.0 / 2.0, 1.0 / 3.0));
    EXPECT_TRUE(
        PointerMatches(events[2].pointer(), 1u, PointerEventPhase::MOVE, 0.0 / 2.0, -4.0 / 3.0));
    EXPECT_TRUE(
        PointerMatches(events[3].pointer(), 1u, PointerEventPhase::MOVE, 5.0 / 2.0, -4.0 / 3.0));
    EXPECT_TRUE(
        PointerMatches(events[4].pointer(), 1u, PointerEventPhase::UP, 5.0 / 2.0, 1.0 / 3.0));
    EXPECT_TRUE(
        PointerMatches(events[5].pointer(), 1u, PointerEventPhase::REMOVE, 5.0 / 2.0, 1.0 / 3.0));
  }
}

// In this test we set up the context and the target. We apply clip space transform to the camera
// and then inject pointer events to confirm that the coordinates received by the listener are
// not impacted by the clip space transform.
TEST_F(CoordinateTransformTest2, ClipSpaceTransformedScene_ShouldHaveNoImpactOnOutput) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();

  // Set up a scene with two ViewHolders, one a child of the other.
  auto [root_session, root_resources] = CreateScene();

  // Set the clip space transform on the camera.
  // Camera zooms in by 3x, and the camera is moved to (24,54) in the scene's coordinate space.
  root_resources.camera.SetClipSpaceTransform(/*x offset*/ 24, /*y offset*/ 54, /*scale*/ 3);

  // Set up the scene.
  scenic::ViewHolder holder_1(root_session.session(), std::move(vh1), "holder_1");
  {
    holder_1.SetViewProperties(k5x5x1);
    root_resources.scene.AddChild(holder_1);
    RequestToPresent(root_session.session());
  }

  SessionWrapper client_1 = CreateClient("view_1", std::move(v1));
  scenic::ViewHolder holder_2(client_1.session(), std::move(vh2), "holder_2");
  {
    holder_2.SetViewProperties(k5x5x1);
    client_1.view()->AddChild(holder_2);
    RequestToPresent(client_1.session());
  }

  SessionWrapper client_2 = CreateClient("view_2", std::move(v2));

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.
  {
    RegisterInjector(client_1.view_ref(), client_2.view_ref());
    Inject(0, 0, fuchsia::ui::input3::PointerEventPhase::ADD);
    Inject(5, 0, fuchsia::ui::input3::PointerEventPhase::CHANGE);
    Inject(5, 5, fuchsia::ui::input3::PointerEventPhase::CHANGE);
    Inject(0, 5, fuchsia::ui::input3::PointerEventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  {
    // Context should receive no events.
    const std::vector<fuchsia::ui::input::InputEvent>& events = client_1.events();
    EXPECT_EQ(events.size(), 0u);
  }

  {  // Target should receive identical events to injected, since their coordinate spaces are the
     // same.
    const std::vector<fuchsia::ui::input::InputEvent>& events = client_2.events();
    ASSERT_EQ(events.size(), 6u);

    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 0.0, 0.0));
    EXPECT_TRUE(PointerMatches(events[1].pointer(), 1u, PointerEventPhase::DOWN, 0.0, 0.0));
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::MOVE, 5.0, 0.0));
    EXPECT_TRUE(PointerMatches(events[3].pointer(), 1u, PointerEventPhase::MOVE, 5.0, 5.0));
    EXPECT_TRUE(PointerMatches(events[4].pointer(), 1u, PointerEventPhase::UP, 0.0, 5.0));
    EXPECT_TRUE(PointerMatches(events[5].pointer(), 1u, PointerEventPhase::REMOVE, 0.0, 5.0));
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
