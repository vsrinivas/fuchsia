// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/input/tests/util.h"

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
  struct Listener : public fuchsia::ui::scenic::PointerCaptureListener {
    Listener() : binding_(this) {}
    ~Listener() { binding_.Close(ZX_OK); }
    // |fuchsia::ui::scenic::PointerCaptureListener|
    void OnPointerEvent(fuchsia::ui::input::PointerEvent event,
                        OnPointerEventCallback callback) override {
      events_.push_back(std::move(event));
      callback();
    }

    fidl::Binding<fuchsia::ui::scenic::PointerCaptureListener> binding_;
    std::vector<fuchsia::ui::input::PointerEvent> events_;
  };

  class ListenerSessionWrapper : public SessionWrapper {
   public:
    ListenerSessionWrapper(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}

    void Register(scenic_impl::input::InputSystem* pointer_capture_registry, ViewRef view_ref,
                  fit::function<void()> run_loop_until_idle) {
      listener_.binding_.set_error_handler([](zx_status_t err) {
        FXL_LOG(ERROR) << "Binding Error: " << zx_status_get_string(err);
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
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }

  std::unique_ptr<ListenerSessionWrapper> CreatePointerCaptureListener(
      const std::string& name, fuchsia::ui::views::ViewToken view_token) {
    auto listener_wrapper = std::make_unique<ListenerSessionWrapper>(scenic());
    auto pair = scenic::ViewRefPair::New();
    ViewRef view_ref_clone;
    pair.view_ref.Clone(&view_ref_clone);
    listener_wrapper->SetViewKoid(scenic_impl::gfx::ExtractKoid(pair.view_ref));
    scenic::View view(listener_wrapper->session(), std::move(view_token),
                      std::move(pair.control_ref), std::move(pair.view_ref), name);
    SetUpTestView(&view);
    listener_wrapper->Register(input_system(), std::move(view_ref_clone),
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
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
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

// Sets up a scene with two views. One captures normal input, the other is the pointer capture
// listener which gets translated off screen and scaled down. The test ensures the input is
// transformed in the expected way to match the view.
TEST_F(PointerCaptureTest, TransformedListenerView_ShouldGetTransformedInput) {
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

    // Translate capture listener client entirely off screen and scale it by 0.5.
    holder_2.SetTranslation(test_display_width_px(), test_display_height_px(), 0);
    holder_2.SetScale(0.5f, 0.5f, 1);

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
    session->Enqueue(pointer.Down(3, 6));
    RequestToPresent(session);
  }

  const std::vector<fuchsia::ui::input::PointerEvent>& events =
      pointerCaptureClient->listener_.events_;
  ASSERT_EQ(events.size(), 2u);

  // Verify capture client gets properly transformed input coordinates.
  EXPECT_TRUE(PointerMatches(events[0], 1u, PointerEventPhase::ADD,
                             (0.5 * 2) + test_display_width_px(),
                             (0.5 * 2) + test_display_height_px()));
  EXPECT_TRUE(PointerMatches(events[1], 1u, PointerEventPhase::DOWN,
                             (0.5 * 3) + test_display_width_px(),
                             (0.5 * 6) + test_display_height_px()));
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

}  // namespace
}  // namespace lib_ui_input_tests
