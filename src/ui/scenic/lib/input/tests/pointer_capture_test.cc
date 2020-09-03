// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/input/tests/util.h"
#include "src/ui/scenic/lib/utils/helpers.h"

// This tests the functionality of the pointer capture API.
//
// The geometry of the display and layer are constrained to a 5x5 square.
//
// Input should always be delivered to the correct session, as well as the listener. In view-local
// coordinates.
//
// NOTE: This test is carefully constructed to avoid Vulkan functionality.

namespace lib_ui_input_tests {
namespace {

using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::views::ViewRef;

// Class fixture for TEST_F. Sets up a 5x5 "display" for GfxSystem.
class PointerCaptureTest : public InputSystemTest {
 public:
  struct Listener : public fuchsia::ui::input::PointerCaptureListener {
    Listener() : binding_(this) {}
    ~Listener() { binding_.Close(ZX_OK); }
    // |fuchsia::ui::input::PointerCaptureListener|
    void OnPointerEvent(fuchsia::ui::input::PointerEvent event,
                        OnPointerEventCallback callback) override {
      events_.push_back(std::move(event));
      callback();
    }

    fidl::Binding<fuchsia::ui::input::PointerCaptureListener> binding_;
    std::vector<fuchsia::ui::input::PointerEvent> events_;
  };

  class ListenerSessionWrapper : public SessionWrapper {
   public:
    ListenerSessionWrapper(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}

    void Register(scenic_impl::input::InputSystem* pointer_capture_registry, ViewRef view_ref,
                  fit::function<void()> run_loop_until_idle) {
      listener_.binding_.set_error_handler([](zx_status_t err) {
        FX_LOGS(ERROR) << "Binding Error: " << zx_status_get_string(err);
      });

      bool register_returned = false;
      pointer_capture_registry->RegisterListener(listener_.binding_.NewBinding(),
                                                 std::move(view_ref),
                                                 [this, &register_returned](bool success) {
                                                   register_returned = true;
                                                   register_successful_ = success;
                                                 });

      run_loop_until_idle();
      ASSERT_TRUE(register_returned);
    }

    Listener listener_;
    bool register_successful_ = false;
  };

 protected:
  uint32_t test_display_width_px() const override { return 9; }
  uint32_t test_display_height_px() const override { return 9; }

  std::unique_ptr<ListenerSessionWrapper> CreatePointerCaptureListener(
      const std::string& name, fuchsia::ui::views::ViewToken view_token) {
    auto listener_wrapper = std::make_unique<ListenerSessionWrapper>(scenic());
    auto pair = scenic::ViewRefPair::New();
    ViewRef view_ref_clone1, view_ref_clone2;
    pair.view_ref.Clone(&view_ref_clone1);
    pair.view_ref.Clone(&view_ref_clone2);
    listener_wrapper->SetViewRef(std::move(view_ref_clone1));
    scenic::View view(listener_wrapper->session(), std::move(view_token),
                      std::move(pair.control_ref), std::move(pair.view_ref), name);
    SetUpTestView(&view);
    listener_wrapper->Register(input_system(), std::move(view_ref_clone2),
                               [this] { RunLoopUntilIdle(); });
    return listener_wrapper;
  }
};

TEST_F(PointerCaptureTest, SingleRegisterAttempt_ShouldSucceed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  std::unique_ptr<ListenerSessionWrapper> client =
      CreatePointerCaptureListener("view", std::move(view_token));
  EXPECT_TRUE(client->register_successful_);
}

TEST_F(PointerCaptureTest, SecondRegisterAttempt_ShouldFail) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  std::unique_ptr<ListenerSessionWrapper> client1 =
      CreatePointerCaptureListener("view1", std::move(view_token1));
  std::unique_ptr<ListenerSessionWrapper> client2 =
      CreatePointerCaptureListener("view2", std::move(view_token2));
  EXPECT_FALSE(client2->register_successful_);
}

TEST_F(PointerCaptureTest, RegisterAttemptAfterDisconnect_ShouldSucceed) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  {
    // Initial registration.
    std::unique_ptr<ListenerSessionWrapper> client =
        CreatePointerCaptureListener("view", std::move(view_token1));
  }  // Disconnect when out of scope.
  {
    // Re-register.
    std::unique_ptr<ListenerSessionWrapper> client =
        CreatePointerCaptureListener("view", std::move(view_token2));
    EXPECT_TRUE(client->register_successful_);
  }
}

// Sets up a scene with a single view, which listens to the pointer capture protocol. The test then
// checks that events are delivered on both channels.
TEST_F(PointerCaptureTest, IfNoOtherView_ThenListenerShouldGetAllInput) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();

  {
    scenic::Session* const session = root_session.session();
    scenic::ViewHolder holder(session, std::move(view_holder_token), "view holder");

    holder.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(holder);
    RequestToPresent(session);
  }

  std::unique_ptr<ListenerSessionWrapper> client =
      CreatePointerCaptureListener("view", std::move(view_token));

  // Scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(2, 2));
    RequestToPresent(session);
  }

  // Regular input path.
  EXPECT_EQ(client->events().size(), 1u);
  // Pointer capture listener path.
  EXPECT_EQ(client->listener_.events_.size(), 1u);
}

// Sets up a scene with two views that can receive input. One view is positioned to receive
// all input through the normal path. The other is moved offscreen and registered to receive input
// through the pointer capture. This test checks that the latter client only gets input on the
// capture path.
TEST_F(PointerCaptureTest, IfAnotherViewGetsInput_ListenerShouldOnlyGetCapturedInput) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();

  {
    scenic::Session* const session = root_session.session();
    scenic::ViewHolder holder_1(session, std::move(view_holder_token1), "holder_1"),
        holder_2(session, std::move(view_holder_token2), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_2.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(holder_1);
    root_resources.scene.AddChild(holder_2);

    // Translate capture listener client entirely off screen.
    holder_2.SetTranslation(test_display_width_px(), test_display_height_px(), 0);

    RequestToPresent(session);
  }

  SessionWrapper regularClient = CreateClient("view", std::move(view_token1));
  std::unique_ptr<ListenerSessionWrapper> pointerCaptureClient =
      CreatePointerCaptureListener("view", std::move(view_token2));

  // Scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(2, 2));
    RequestToPresent(session);
  }

  EXPECT_EQ(regularClient.events().size(), 1u);
  EXPECT_TRUE(pointerCaptureClient->events().empty());
  EXPECT_EQ(pointerCaptureClient->listener_.events_.size(), 1u);
}

TEST_F(PointerCaptureTest, WhenParellelDispatchOn_ShouldOnlyGetOneEvent) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();
  scenic::Session* const session = root_session.session();

  {
    scenic::ViewHolder holder_1(session, std::move(view_holder_token1), "holder_1"),
        holder_2(session, std::move(view_holder_token2), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_2.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(holder_1);
    root_resources.scene.AddChild(holder_2);

    // Translate clients so they're not overlapping, but both would be hit by the same input.
    holder_1.SetTranslation(0, 0, -1);
    holder_2.SetTranslation(0, 0, 1);

    RequestToPresent(session);
  }

  SessionWrapper regularClient = CreateClient("view", std::move(view_token1));
  std::unique_ptr<ListenerSessionWrapper> pointerCaptureClient =
      CreatePointerCaptureListener("view", std::move(view_token2));

  // Scene is now set up, send in the input.
  {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(4, 4));

    RequestToPresent(session);
  }

  EXPECT_EQ(regularClient.events().size(), 1u);
  EXPECT_EQ(pointerCaptureClient->events().size(), 1u);
  EXPECT_EQ(pointerCaptureClient->listener_.events_.size(), 1u);
}

TEST_F(PointerCaptureTest, WhenListenerDisconnects_OtherClientsShouldStillWork) {
  auto [view_token1, view_holder_token1] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();
  scenic::Session* const session = root_session.session();

  {
    scenic::ViewHolder holder_1(session, std::move(view_holder_token1), "holder_1"),
        holder_2(session, std::move(view_holder_token2), "holder_2");

    holder_1.SetViewProperties(k5x5x1);
    holder_2.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(holder_1);
    root_resources.scene.AddChild(holder_2);

    // Translate capture client so it doesn't get input.
    holder_2.SetTranslation(test_display_width_px(), test_display_height_px(), 0);

    RequestToPresent(session);
  }

  SessionWrapper regularClient = CreateClient("view", std::move(view_token1));
  {
    std::unique_ptr<ListenerSessionWrapper> pointerCaptureClient =
        CreatePointerCaptureListener("view", std::move(view_token2));

    // Scene is now set up, send in the input.
    {
      PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                      /*pointer id*/ 1, PointerEventType::TOUCH);
      // Sent in as device (display) coordinates.
      session->Enqueue(pointer.Add(4, 4));

      RequestToPresent(session);
    }

    EXPECT_EQ(regularClient.events().size(), 1u);
    EXPECT_EQ(pointerCaptureClient->listener_.events_.size(), 1u);
  }  // pointerCaptureClient goes out of scope

  // Get ready for new input.
  regularClient.events().clear();

  // Send more input
  {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 2, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(2, 2));
    RequestToPresent(session);
  }

  EXPECT_EQ(regularClient.events().size(), 1u);
}

// Sets up a scene with a single view capturing input both throught he normal channel and the
// pointer capture. Then checks that the values in both channels match.
TEST_F(PointerCaptureTest, CapturedInputCoordinates_ShouldMatch_RegularInputCoordinates) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();

  {
    scenic::Session* const session = root_session.session();
    scenic::ViewHolder holder(session, std::move(view_holder_token), "view holder");

    holder.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(holder);
    RequestToPresent(session);
  }

  std::unique_ptr<ListenerSessionWrapper> client =
      CreatePointerCaptureListener("view", std::move(view_token));

  // Scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(3, 6));
    RequestToPresent(session);
  }

  // Verify client gets all expected touch events through both channels.
  {
    const std::vector<InputEvent>& events = client->events();

    ASSERT_EQ(events.size(), 3u);

    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 2, 2));

    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 3, 6));
  }

  {
    const std::vector<fuchsia::ui::input::PointerEvent>& events = client->listener_.events_;
    ASSERT_EQ(events.size(), 2u);
    // View covers display exactly, so view coordinates match display coordinates.
    EXPECT_TRUE(PointerMatches(events[0], 1u, PointerEventPhase::ADD, 2, 2));
    EXPECT_TRUE(PointerMatches(events[1], 1u, PointerEventPhase::DOWN, 3, 6));
  }
}

// Sets up a scene and creates a view for capturing input events, but never attaches it to the
// scene. Checks that no events are captured.
TEST_F(PointerCaptureTest, IfViewUnattached_ListenerShouldGetNoInput) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();

  {
    scenic::Session* const session = root_session.session();
    scenic::ViewHolder holder(session, std::move(view_holder_token), "view holder");
    RequestToPresent(session);
  }

  std::unique_ptr<ListenerSessionWrapper> client =
      CreatePointerCaptureListener("view", std::move(view_token));

  // Scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(2, 2));
    RequestToPresent(session);
  }

  EXPECT_TRUE(client->listener_.events_.empty());
}

// Sets up a scene, attaches and then detaches a view for capturing input events.
// Checks that no events are captured.
TEST_F(PointerCaptureTest, IfViewDetached_ListenerShouldGetNoInput) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();

  {
    scenic::Session* const session = root_session.session();
    scenic::ViewHolder holder(session, std::move(view_holder_token), "view holder");

    holder.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(holder);
    RequestToPresent(session);
    holder.Detach();
    RequestToPresent(session);
  }

  std::unique_ptr<ListenerSessionWrapper> client =
      CreatePointerCaptureListener("view", std::move(view_token));

  // Scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(2, 2));
    RequestToPresent(session);
  }

  EXPECT_TRUE(client->listener_.events_.empty());
}

// In this test we set up a view, apply a transform to the view holder node, and then send pointer
// events to confirm that the coordinates received by the listener are correctly transformed.
//
// Below are ASCII diagrams showing the scene setup.
// Note that the notated X,Y coordinate system is the screen coordinate system. The view's
// coordinate system has its origin at corner '1'.
//
// Scene pre-transformation (1,2,3,4 denote the corners of the view):
// Note that the view's coordinate system is the same as the screen coordinate system.
// Scene pre-transformation (1,2,3,4 denote the corners of the target view):
//   X ->
// Y 1 O O O O 2 - - - -
// | O O O O O O - - - -
// v O O O O O O - - - -
//   O O O O O O - - - -
//   O O O O O O - - - -
//   4 O O O O 3 - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//   - - - - - - - - - -
//
// After scale:
//   X ->
// Y 1 - O - O - O   O   2
// | - - - - - - - - - -
// V - - - - - - - - - -
//   O - O - O - O - O - O
//   - - - - - - - - - -
//   - - - - - - - - - -
//   O - O - O - O - O - O
//   - - - - - - - - - -
//   - - - - - - - - - -
//   O - O - O - O - O - O
//
//
//   O   O   O   O   O   O
//
//
//   4   O   O   O   O   3
//
// After rotation:
//   X ->
// Y 4      O      O      O      O      1 - - - - - - - - - -
// |                                      - - - - - - - - - -
// V O      O      O      O      O      O - - - - - - - - - -
//                                        - - - - - - - - - -
//   O      O      O      O      O      O - - - - - - - - - -
//                                        - - - - - - - - - -
//   O      O      O      O      O      O - - - - - - - - - -
//                                        - - - - - - - - - -
//   O      O      O      O      O      O - - - - - - - - - -
//                                        - - - - - - - - - -
//   3      O      O      O      O      2
//
// After translation:
//   X ->
// Y 4      O      O      O      O    - 1 - - - - - - - - -
// |                                  - - - - - - - - - - -
// V O      O      O      O      O    - O - - - - - - - - -
//                                    - - - - - - - - - - -
//   O      O      O      O      O    - O - - - - - - - - -
//                                    - - - - - - - - - - -
//   O      O      O      O      O    - O - - - - - - - - -
//                                    - - - - - - - - - - -
//   O      O      O      O      O    - O - - - - - - - - -
//                                    - - - - - - - - - - -
//   3      O      O      O      O      2

TEST_F(PointerCaptureTest, TransformedListenerView_ShouldGetTransformedInput) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();

  {
    scenic::Session* const session = root_session.session();
    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "view_holder");

    view_holder.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(view_holder);

    // Scale, rotate and translate capture listener client.
    // Scale X by 2 and Y by 3.
    view_holder.SetScale(2, 3, 1);
    // Rotate 90 degrees counter clockwise around Z-axis (Z-axis points into screen, so appears as
    // clockwise rotation).
    const auto rotation_quaternion = glm::angleAxis(glm::pi<float>() / 2.f, glm::vec3(0, 0, 1));
    view_holder.SetRotation(rotation_quaternion.x, rotation_quaternion.y, rotation_quaternion.z,
                            rotation_quaternion.w);
    // Translate by 1 in the X direction.
    view_holder.SetTranslation(1, 0, 0);

    RequestToPresent(session);
  }

  std::unique_ptr<ListenerSessionWrapper> pointerCaptureClient =
      CreatePointerCaptureListener("view", std::move(view_token));

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation. Injected events are in the screen coordinate space.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(0, 0));
    session->Enqueue(pointer.Down(5, 0));
    session->Enqueue(pointer.Move(5, 5));
    session->Enqueue(pointer.Up(0, 5));
    RequestToPresent(session);
  }

  {  // Received events should be in the coordinate space of the view.
    const std::vector<fuchsia::ui::input::PointerEvent>& events =
        pointerCaptureClient->listener_.events_;
    ASSERT_EQ(events.size(), 4u);

    // Verify capture client gets properly transformed input coordinates.
    EXPECT_TRUE(PointerMatches(events[0], 1u, PointerEventPhase::ADD, 0.0 / 2.0, 1.0 / 3.0));
    EXPECT_TRUE(PointerMatches(events[1], 1u, PointerEventPhase::DOWN, 0.0 / 2.0, -4.0 / 3.0));
    EXPECT_TRUE(PointerMatches(events[2], 1u, PointerEventPhase::MOVE, 5.0 / 2.0, -4.0 / 3.0));
    EXPECT_TRUE(PointerMatches(events[3], 1u, PointerEventPhase::UP, 5.0 / 2.0, 1.0 / 3.0));
  }
}

// In this test we set up a view, apply a ClipSpaceTransform to the camera, and then send pointer
// events to confirm that the coordinates received by the listener are correctly transformed.
//
// Below are ASCII diagrams showing the scene setup.
// Note that the notated X,Y coordinate system is the screen coordinate system. The view's
// coordinate system has its origin at corner '1'.
//
// Scene pre-transformation (1,2,3,4 denote the corners of the view):
// Note that the view's coordinate system is the same as the screen coordinate system.
//   X ->
// Y 1 O O O 2 - - - -
// | O O O O O - - - -
// v O O O O O - - - -
//   O O O O O - - - -
//   4 O O O 3 - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//
// Scene after scale, before offset:
// 1   O   O   O   2
//
// O   O   O   O   O
//
// O   O   O - O - O - - - -
//         - - - - - - - - -
// O   O   O - O - O - - - -
//         - - - - - - - - -
// 4   O   O - O - 3 - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//
// Scene post-scale, post-offset:
// The X and Y dimensions of the view are now effectively scaled up to 10x10
// (compared to the 9x9 of the screen), with origin at screen space origin.
//   X ->
// Y 1D- O - M1- O - 2
// | - - - - - - - - -
// V O   O   O   O   O
//   - - - - - - - - -
//   U - O - M2- O - O
//   - - - - - - - - -
//   O   O - O - O - O
//   - - - - - - - - -
//   4 - O - O - O - 3
//
//  D     - Down event
//  M1,M2 - Move events
//  U     - Up event
TEST_F(PointerCaptureTest, ClipSpaceTransformedListenerView_ShouldGetTransformedInput) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();

  static constexpr float kScaleX = 0.5f;
  static constexpr float kScaleY = 1.5f;
  const float width = static_cast<float>(test_display_width_px());
  const float height = static_cast<float>(test_display_height_px());

  {
    scenic::Session* const session = root_session.session();
    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "view_holder");

    view_holder.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(view_holder);

    // Set the clip space transform on the camera.
    // The transform scales everything by 2 around the center of the screen (4.5, 4.5) and then
    // applies offsets in Vulkan normalized device coordinates to bring the origin back
    // to where it was originally. (Parameters are in Vulkan Normalized Device Coordinates.)
    root_resources.camera.SetClipSpaceTransform(/*x offset*/ 1, /*y offset*/ 1, /*scale*/ 2);

    RequestToPresent(session);
  }

  std::unique_ptr<ListenerSessionWrapper> pointerCaptureClient =
      CreatePointerCaptureListener("view", std::move(view_token));

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation. Injected events are in the screen coordinate space.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(0, 0));
    session->Enqueue(pointer.Down(5, 0));
    session->Enqueue(pointer.Move(5, 5));
    session->Enqueue(pointer.Up(0, 5));
    RequestToPresent(session);
  }

  {  // Received events should be in the coordinate space of the view.
    const std::vector<fuchsia::ui::input::PointerEvent>& events =
        pointerCaptureClient->listener_.events_;
    ASSERT_EQ(events.size(), 4u);

    // Verify capture client gets properly transformed input coordinates.
    EXPECT_TRUE(PointerMatches(events[0], 1u, PointerEventPhase::ADD, 0, 0));
    EXPECT_TRUE(PointerMatches(events[1], 1u, PointerEventPhase::DOWN, 2.5, 0));
    EXPECT_TRUE(PointerMatches(events[2], 1u, PointerEventPhase::MOVE, 2.5, 2.5));
    EXPECT_TRUE(PointerMatches(events[3], 1u, PointerEventPhase::UP, 0, 2.5));
  }
}

// In this test we set up a view, apply a ClipSpaceTransform scale to the camera as well as a
// translation on the view holder, and confirm that the delivered coordinates are correctly
// transformed.
//
// Below are ASCII diagrams showing the scene setup.
// Note that the notated X,Y coordinate system is the screen coordinate system. The view's
// coordinate system has its origin at corner '1'.
//
// Scene pre-transformation (1,2,3,4 denote the corners of the view):
// Note that the view's coordinate system is the same as the screen coordinate system.
//   X ->
// Y 1 O O O 2 - - - -
// | O O O O O - - - -
// v O O O O O - - - -
//   O O O O O - - - -
//   4 O O O 3 - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//   - - - - - - - - -
//
// Scene after post-clip space transform, pre-translation:
// 1   O   O   O   2
//
// O   O   O   O   O
//
// O   O   O - O - O - - - -
//         - - - - - - - - -
// O   O   O - O - O - - - -
//         - - - - - - - - -
// 4   O   O - O - 3 - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//         - - - - - - - - -
//
// Scene after post-clip space transform, post-translation:
// Size of view is effectively 10x10, translated by (1,1).
//   X ->
// Y 1   O   O   O   2
// |
// V O   D - O - O M1- -
//       - - - - - - - - -
//   O   O - O - O - O - -
//       - - - - - - - - -
//   O   O - O - O - O - -
//       U - - - - M2- - -
//   4   O - O - O - 3 - -
//       - - - - - - - - -
//       - - - - - - - - -
//
//  D     - Down event
//  M1,M2 - Move events
//  U     - Up event
TEST_F(PointerCaptureTest,
       ClipSpaceAndNodeTransformedListenerView_ShouldGetCorrectlyTransformedInput) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();
  uint32_t compositor_id = root_resources.compositor.id();

  {
    scenic::Session* const session = root_session.session();
    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "view_holder");

    view_holder.SetViewProperties(k5x5x1);

    root_resources.scene.AddChild(view_holder);

    // Set the clip space transform to zoom in on the center of the screen.
    root_resources.camera.SetClipSpaceTransform(0, 0, /*scale*/ 2);
    // Translate view holder.
    view_holder.SetTranslation(1, 1, 0);

    RequestToPresent(session);
  }

  std::unique_ptr<ListenerSessionWrapper> pointerCaptureClient =
      CreatePointerCaptureListener("view", std::move(view_token));

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation. Injected events are in the screen coordinate space.
  {
    scenic::Session* const session = root_session.session();

    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Sent in as device (display) coordinates.
    session->Enqueue(pointer.Add(0, 0));
    session->Enqueue(pointer.Down(5, 0));
    session->Enqueue(pointer.Move(5, 5));
    session->Enqueue(pointer.Up(0, 5));
    RequestToPresent(session);
  }

  {  // Received events should be in the coordinate space of the view.
    const std::vector<fuchsia::ui::input::PointerEvent>& events =
        pointerCaptureClient->listener_.events_;
    ASSERT_EQ(events.size(), 4u);

    // Verify capture client gets properly transformed input coordinates.
    EXPECT_TRUE(PointerMatches(events[0], 1u, PointerEventPhase::ADD, 2.25 - 1, 2.25 - 1));
    EXPECT_TRUE(PointerMatches(events[1], 1u, PointerEventPhase::DOWN, 4.75 - 1, 2.25 - 1));
    EXPECT_TRUE(PointerMatches(events[2], 1u, PointerEventPhase::MOVE, 4.75 - 1, 4.75 - 1));
    EXPECT_TRUE(PointerMatches(events[3], 1u, PointerEventPhase::UP, 2.25 - 1, 4.75 - 1));
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
