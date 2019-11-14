// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/input/tests/util.h"
#include "src/ui/scenic/lib/scenic/scenic.h"

// This test exercises focus transfer logic when touch or mouse events are involved.
//
// A pointer DOWN event typically triggers a pair of focus/unfocus events, each sent to a client.
// However, when the DOWN event does not have associated views, then focus should revert to the root
// of a valid focus chain.
//
// The geometry is constrained to a 9x9 display and layer. We need one root session to set up the
// Scene (with no geometry), and two ordinary sessions to each set up its 5x5 View. The spatial
// layout is as follows:
//
// - - - - - - - - -    (invisible) - scene's origin translated to (1,1), relative to display
// - 1 1 1 1 1 - - -    1 - view 1: a 5x5 square, origin coincides with scene origin
// - 1 1 1 1 1 - y -        (z depth is 1 - lower than view 2)
// - 1 1 2 2 2 2 x -    2 - view 2: a 5x5 square, origin translated (2,2) from scene origin
// - 1 1 2 2 2 2 2 -        (z depth is 0 - higher than view 1)
// - 1 1 2 2 2 2 2 -    x - touch/mouse down on view 2: focus transfers to view 2
// - - - 2 2 2 2 2 -    y - touch/mouse down outside of view: focus transfers to scene
// - - - 2 2 2 2 2 -
// - - - - - - - - -
//
// The scene graph has the following topology:
//         scene
//        /     \
//   holder 1   holder 2
//       |        |
//    view 1     view 2
//
// To create this test setup, we perform translation of each holder (a (0,0,1) and (2,2,0)
// translation for each view holder, respectively, within the scene), in addition to translating the
// Rectangle shape within each view's space (a constant (2,2) translation). Setup finishes by
// transferring focus to view 1.
//
// The first (ADD, DOWN) touch sequence, on x, should successfully transfer focus to view 2.
// The second (ADD, DOWN) touch sequence, on y, should successfully transfer focus to the scene.

namespace src_ui_scenic_lib_input_tests {

using A11yPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using A11yStreamResponse = fuchsia::ui::input::accessibility::EventHandling;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
using lib_ui_input_tests::InputSystemTest;
using lib_ui_input_tests::PointerCommandGenerator;
using lib_ui_input_tests::PointerMatches;
using lib_ui_input_tests::ResourceGraph;
using lib_ui_input_tests::SessionWrapper;
using scenic_impl::gfx::ExtractKoid;
using scenic_impl::gfx::ViewTree;

// Class fixture for TEST _F. Sets up a 9x9 "display".
class FocusTransferTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 9; }
  uint32_t test_display_height_px() const override { return 9; }

  // Accessors.
  SessionWrapper* root_session() { return root_session_.get(); }
  ResourceGraph* root_resources() { return root_resources_.get(); }
  SessionWrapper* client_1() { return client_1_.get(); }
  SessionWrapper* client_2() { return client_2_.get(); }

  void ClearEventsInAllSessions() {
    if (root_session_)
      root_session_->events().clear();
    if (client_1_)
      client_1_->events().clear();
    if (client_2_)
      client_2_->events().clear();
  }

 private:
  // Scene setup.
  void SetUp() override {
    InputSystemTest::SetUp();

    auto view_pair_1 = scenic::ViewTokenPair::New();  // root - client 1
    auto view_pair_2 = scenic::ViewTokenPair::New();  // root - client 2

    // Set up a scene with two views.
    auto [root_session, root_resources] = CreateScene();
    {
      scenic::Session* const session = root_session.session();
      scenic::Scene* const scene = &root_resources.scene;

      // Translate the scene.
      scene->SetTranslation(1, 1, 0);

      // Attach the translated view holders.
      scenic::ViewHolder holder_1(session, std::move(view_pair_1.view_holder_token), "holder_1"),
          holder_2(session, std::move(view_pair_2.view_holder_token), "holder_2");

      holder_1.SetViewProperties(k5x5x1);
      holder_2.SetViewProperties(k5x5x1);

      scene->AddChild(holder_1);
      holder_1.SetTranslation(0, 0, 1);  // View 1's origin coincides with Scene's origin.

      scene->AddChild(holder_2);
      holder_2.SetTranslation(2, 2, 0);  // View 2's origin translated (2, 2) wrt Scene's origin.

      RequestToPresent(session);
    }

    // Clients.
    SessionWrapper client_1 = CreateClient("View 1", std::move(view_pair_1.view_token)),
                   client_2 = CreateClient("View 2", std::move(view_pair_2.view_token));

    // Transfer focus to client 1.
    {
      root_session.SetViewKoid(engine()->scene_graph()->view_tree().focus_chain()[0]);
      auto status =
          engine()->scene_graph()->RequestFocusChange(root_session.ViewKoid(), client_1.ViewKoid());
      ASSERT_EQ(status, ViewTree::FocusChangeStatus::kAccept);

      RunLoopUntilIdle();  // Flush out focus events to clients.
    }

    // Transfer ownership to test fixture.
    root_session_ = std::make_unique<SessionWrapper>(std::move(root_session));
    root_resources_ = std::make_unique<ResourceGraph>(std::move(root_resources));
    client_1_ = std::make_unique<SessionWrapper>(std::move(client_1));
    client_2_ = std::make_unique<SessionWrapper>(std::move(client_2));

    ClearEventsInAllSessions();
  }

  void TearDown() override {
    root_resources_ = nullptr;
    root_session_ = nullptr;
    client_1_ = nullptr;
    client_2_ = nullptr;
    InputSystemTest::TearDown();
  }

  std::unique_ptr<SessionWrapper> root_session_;
  std::unique_ptr<ResourceGraph> root_resources_;
  std::unique_ptr<SessionWrapper> client_1_;
  std::unique_ptr<SessionWrapper> client_2_;
};

// Some tests require the presence of an accessibility listener to trigger pointer interception.
class A11yListener : public fuchsia::ui::input::accessibility::PointerEventListener {
 public:
  A11yListener(scenic_impl::input::InputSystem* input_system) : listener_binding_(this) {
    input_system->Register(listener_binding_.NewBinding(),
                           [](bool success) { ASSERT_TRUE(success); });
  }

 private:
  // |fuchsia::ui::input::accessibility::PointerEventListener|
  // Simple response: always reject on MOVE event.
  void OnEvent(A11yPointerEvent event) override {
    if (event.phase() == PointerEventPhase::MOVE) {
      listener_binding_.events().OnStreamHandled(event.device_id(), event.pointer_id(),
                                                 A11yStreamResponse::REJECTED);
    }
  }

  fidl::Binding<fuchsia::ui::input::accessibility::PointerEventListener> listener_binding_;
};

// Normally, focus gets transferred to a valid target on the DOWN phase.
TEST_F(FocusTransferTest, TouchFocusWithValidTarget) {
  // Inject ADD/DOWN on client 2 to trigger focus dispatch.
  {
    scenic::Session* const session = root_session()->session();

    PointerCommandGenerator finger(root_resources()->compositor.id(), /*device id*/ 1,
                                   /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(finger.Add(7, 3));
    session->Enqueue(finger.Down(7, 3));
  }
  RunLoopUntilIdle();

  // Verify client 1 receives unfocus event.
  {
    const std::vector<InputEvent>& events = client_1()->events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_FALSE(events[0].focus().focused);
  }

  // Verify client 2 receives focus event.
  {
    const std::vector<InputEvent>& events = client_2()->events();
    ASSERT_EQ(events.size(), 3u);

    // ADD
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 4.5, 0.5));

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    // DOWN
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 4.5, 0.5));
  }

  // Verify root session receives nothing.
  {
    const std::vector<InputEvent>& events = root_session()->events();
    EXPECT_EQ(events.size(), 0u);
  }
}

// Sometimes, focus does not have a valid target; instead, transfer focus to the root of the focus
// chain, which is the Scene-creating session in GFX.
TEST_F(FocusTransferTest, TouchFocusWithInvalidTarget) {
  // Inject ADD/DOWN outside of clients to trigger focus dispatch.
  {
    scenic::Session* const session = root_session()->session();

    PointerCommandGenerator finger(root_resources()->compositor.id(), /*device id*/ 1,
                                   /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(finger.Add(7, 2));
    session->Enqueue(finger.Down(7, 2));
  }
  RunLoopUntilIdle();

  // Verify client 1 receives unfocus event.
  {
    const std::vector<InputEvent>& events = client_1()->events();
    ASSERT_EQ(events.size(), 1u);

    EXPECT_TRUE(events[0].is_focus());
    EXPECT_FALSE(events[0].focus().focused);
  }

  // Verify client 2 receives nothing, since nothing was hit.
  {
    const std::vector<InputEvent>& events = client_2()->events();
    EXPECT_EQ(events.size(), 0u);
  }

  // Verify root session receives focus event, since we revert to root of focus chain.
  {
    const std::vector<InputEvent>& events = root_session()->events();
    ASSERT_EQ(events.size(), 1u);

    EXPECT_TRUE(events[0].is_focus());
    EXPECT_TRUE(events[0].focus().focused);
  }
}

// When a valid but unfocused target (client 2) receives an ADD event and DOWN event, and then the
// scene disconnects, the target receives an unfocus event (where focus=false).
TEST_F(FocusTransferTest, TouchFocusDisconnectSceneAfterDown) {
  // Inject ADD/DOWN on client 2 to trigger focus dispatch.
  {
    scenic::Session* const session = root_session()->session();

    PointerCommandGenerator finger(root_resources()->compositor.id(), /*device id*/ 1,
                                   /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(finger.Add(7, 3));
    session->Enqueue(finger.Down(7, 3));
  }
  RunLoopUntilIdle();

  ClearEventsInAllSessions();

  // Disconnect scene from compositor.
  {
    scenic::Session* const session = root_session()->session();

    scenic::LayerStack alternate_layer_stack(session);
    root_resources()->compositor.SetLayerStack(alternate_layer_stack);
    RequestToPresent(session);
  }

  // Verify client 2 receives unfocus event.
  {
    const std::vector<InputEvent>& events = client_2()->events();
    ASSERT_EQ(events.size(), 1u);

    // FOCUS
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_FALSE(events[0].focus().focused);
  }

  // Verify client 1 receives nothing.
  {
    const std::vector<InputEvent>& events = client_1()->events();
    EXPECT_EQ(events.size(), 0u);
  }

  // Verify root session receives nothing.
  {
    const std::vector<InputEvent>& events = root_session()->events();
    EXPECT_EQ(events.size(), 0u);
  }
}

// Ensure TouchFocusWithValidTarget works after accessibility rejects the pointer stream.
TEST_F(FocusTransferTest, TouchFocusWithValidTargetAfterA11yRejects) {
  A11yListener a11y_listener(input_system());  // Turn on accessibility interception.
  RunLoopUntilIdle();                          // Ensure FIDL calls get processed.

  // Inject ADD/DOWN on client 2 to trigger delayed focus dispatch.
  {
    scenic::Session* const session = root_session()->session();

    PointerCommandGenerator finger(root_resources()->compositor.id(), /*device id*/ 1,
                                   /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(finger.Add(7, 3));
    session->Enqueue(finger.Down(7, 3));
  }
  RunLoopUntilIdle();

  // Ordinary clients should not see focus events.
  EXPECT_EQ(client_1()->events().size(), 0u);
  EXPECT_EQ(client_2()->events().size(), 0u);
  EXPECT_EQ(root_session()->events().size(), 0u);

  // Inject MOVE to trigger a11y rejection.
  // Inject ADD/DOWN on client 2 to trigger delayed focus dispatch.
  {
    scenic::Session* const session = root_session()->session();

    PointerCommandGenerator finger(root_resources()->compositor.id(), /*device id*/ 1,
                                   /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(finger.Move(7, 3));
  }
  RunLoopUntilIdle();

  // A11y rejection of MOVE should cause event dispatch to ordinary clients.

  // Verify client 1 receives unfocus event.
  {
    const std::vector<InputEvent>& events = client_1()->events();
    ASSERT_EQ(events.size(), 1u);

    // FOCUS
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_FALSE(events[0].focus().focused);
  }

  // Verify  client 2 receives focus event.
  {
    const std::vector<InputEvent>& events = client_2()->events();
    ASSERT_EQ(events.size(), 4u);

    // ADD
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::ADD, 4.5, 0.5));

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    // DOWN
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 4.5, 0.5));

    // MOVE
    EXPECT_TRUE(events[3].is_pointer());
    EXPECT_TRUE(PointerMatches(events[3].pointer(), 1u, PointerEventPhase::MOVE, 4.5, 0.5));
  }

  // Verify root session receives nothing.
  {
    const std::vector<InputEvent>& events = root_session()->events();
    EXPECT_EQ(events.size(), 0u);
  }
}

// Ensure TouchFocusWithInvalidTarget works after accessibility rejects the pointer stream.
TEST_F(FocusTransferTest, TouchFocusWithInvalidTargetAfterA11yRejects) {
  A11yListener a11y_listener(input_system());  // Turn on accessibility interception.
  RunLoopUntilIdle();                          // Ensure FIDL calls get processed.

  // Inject ADD, DOWN, and MOVE (the MOVE triggers a11y rejection).
  {
    scenic::Session* const session = root_session()->session();

    PointerCommandGenerator finger(root_resources()->compositor.id(), /*device id*/ 1,
                                   /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(finger.Add(7, 2));
    session->Enqueue(finger.Down(7, 2));
    session->Enqueue(finger.Move(7, 2));
  }
  RunLoopUntilIdle();

  // A11y rejection of MOVE should cause focus event dispatch to ordinary clients.
  // However, there was no latch on DOWN, so nothing should see pointer events.

  // Verify client 1 receives unfocus event.
  {
    const std::vector<InputEvent>& events = client_1()->events();
    ASSERT_EQ(events.size(), 1u);

    // FOCUS
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_FALSE(events[0].focus().focused);
  }

  // Verify client 2 receives nothing, since nothing was hit.
  {
    const std::vector<InputEvent>& events = client_2()->events();
    EXPECT_EQ(events.size(), 0u);
  }

  // Verify root session receives focus event, since we revert to root of focus chain.
  {
    const std::vector<InputEvent>& events = root_session()->events();
    ASSERT_EQ(events.size(), 1u);

    // FOCUS
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_TRUE(events[0].focus().focused);
  }
}

// Normally, focus gets transferred to a valid target on the DOWN phase.
TEST_F(FocusTransferTest, MouseFocusWithValidTarget) {
  // Inject ADD/DOWN on client 2 to trigger delayed focus dispatch.
  {
    scenic::Session* const session = root_session()->session();

    PointerCommandGenerator finger(root_resources()->compositor.id(), /*device id*/ 1,
                                   /*pointer id*/ 1, PointerEventType::MOUSE);
    session->Enqueue(finger.Move(7, 3));
    session->Enqueue(finger.Down(7, 3));
  }
  RunLoopUntilIdle();

  // Verify client 1 receives unfocus event.
  {
    const std::vector<InputEvent>& events = client_1()->events();
    ASSERT_EQ(events.size(), 1u);

    // FOCUS
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_FALSE(events[0].focus().focused);
  }

  // Verify client 2 receives focus event.
  {
    const std::vector<InputEvent>& events = client_2()->events();
    ASSERT_EQ(events.size(), 3u);

    // MOVE
    EXPECT_TRUE(events[0].is_pointer());
    EXPECT_TRUE(PointerMatches(events[0].pointer(), 1u, PointerEventPhase::MOVE, 4.5, 0.5));

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());
    EXPECT_TRUE(events[1].focus().focused);

    // DOWN
    EXPECT_TRUE(events[2].is_pointer());
    EXPECT_TRUE(PointerMatches(events[2].pointer(), 1u, PointerEventPhase::DOWN, 4.5, 0.5));
  }

  // Verify root session receives nothing.
  {
    const std::vector<InputEvent>& events = root_session()->events();
    EXPECT_EQ(events.size(), 0u);
  }
}

// Sometimes, focus does not have a valid target; instead, transfer focus to the root of the focus
// chain, which is the Scene-creating session in GFX.
TEST_F(FocusTransferTest, MouseFocusWithInvalidTarget) {
  // Inject ADD/DOWN outside of clients to trigger focus dispatch.
  {
    scenic::Session* const session = root_session()->session();

    PointerCommandGenerator finger(root_resources()->compositor.id(), /*device id*/ 1,
                                   /*pointer id*/ 1, PointerEventType::MOUSE);
    session->Enqueue(finger.Move(7, 2));
    session->Enqueue(finger.Down(7, 2));
  }
  RunLoopUntilIdle();

  // Verify client 1 receives unfocus event.
  {
    const std::vector<InputEvent>& events = client_1()->events();
    ASSERT_EQ(events.size(), 1u) << "Should receive exactly 1 input event.";

    // FOCUS
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_FALSE(events[0].focus().focused);
  }

  // Verify client 2 receives nothing, since nothing was hit.
  {
    const std::vector<InputEvent>& events = client_2()->events();
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 input events.";
  }

  // Verify root session receives focus event, since we revert to root of focus chain.
  {
    const std::vector<InputEvent>& events = root_session()->events();
    ASSERT_EQ(events.size(), 1u) << "Should receive exactly 1 input event.";

    // FOCUS
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_TRUE(events[0].focus().focused);
  }
}

}  // namespace src_ui_scenic_lib_input_tests
