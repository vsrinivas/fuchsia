// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/eventpair.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/input/tests/util.h"

namespace lib_ui_input_tests {

// common test setups:
//
// In each test case, a basic Scenic scene will be created, as well as a client
// that will connect to it. This client will also register an accessibility
// listener with the input system. After, some tests will exercise the injection
// of pointer events into the session. Depending on the accessibility listener
// response, configured with client.ConfigureResponses(), the pointer events
// will be consumed / rejected.
// When they are consumed, we test that the client is not receiving any events.
// When they are rejected, the test makes sure that we received the pointer
// events in the listener and the client also got them.

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using InputCommand = fuchsia::ui::input::Command;
using Phase = fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::views::ViewHolderToken;
using fuchsia::ui::views::ViewToken;
using scenic_impl::gfx::ExtractKoid;

// Class fixture for TEST_F. Sets up a 5x5 "display" for GfxSystem.
class AccessibilityPointerEventsTest : public InputSystemTest {
 public:
  // Sets up a minimal scene.
  void SetupScene(scenic::Session* session, scenic::EntityNode* root_node, uint32_t* compositor_id,
                  ViewHolderToken vh_token) {
    // Minimal scene.
    scenic::Compositor compositor(session);
    *compositor_id = compositor.id();

    scenic::Scene scene(session);
    scenic::Camera camera(scene);
    scenic::Renderer renderer(session);
    renderer.SetCamera(camera);

    scenic::Layer layer(session);
    layer.SetSize(test_display_width_px(), test_display_height_px());
    layer.SetRenderer(renderer);

    scenic::LayerStack layer_stack(session);
    layer_stack.AddLayer(layer);
    compositor.SetLayerStack(layer_stack);

    // Add local root node to the scene, attach the view holder.
    scene.AddChild(*root_node);
    scenic::ViewHolder view_holder(session, std::move(vh_token), "View Holder");
    // Create view bound for this view.
    const float kZero[3] = {0, 0, 0};
    view_holder.SetViewProperties(kZero, (float[3]){5, 5, 1}, kZero, kZero);
    root_node->Attach(view_holder);
    RequestToPresent(session);
  }

 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }
};

// A wrapper class around SessionWrapper with some utilities to configure
// clients. also implements a fake interface for the accessibility listener and
// Accessibility Manager, which, in a real scenario would be responsible for
// processing the incoming accessibility pointer events.
class AccessibilityPointerEventListenerSessionWrapper
    : public SessionWrapper,
      public fuchsia::ui::input::accessibility::PointerEventListener {
 public:
  AccessibilityPointerEventListenerSessionWrapper(scenic_impl::Scenic* scenic)
      : SessionWrapper(scenic) {}
  ~AccessibilityPointerEventListenerSessionWrapper() = default;

  void ClearEvents() { events_.clear(); }
  void ClearAccessibilityEvents() { accessibility_pointer_events_.clear(); }

  bool IsListenerRegistered() { return listener_registered_; }

  void SetupAccessibilityPointEventListener(AccessibilityPointerEventsTest* test) {
    fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener> listener_handle =
        listener_bindings_.AddBinding(this);
    auto callback = [this](bool success) { listener_registered_ = success; };
    test->RegisterAccessibilityListener(std::move(listener_handle), std::move(callback));
  }

  void DisconnectListener() {
    listener_bindings_.CloseAll();
    listener_registered_ = false;
  }

  void ExamineAccessibilityPointerEvents(
      fit::function<void(const std::vector<AccessibilityPointerEvent>& events)>
          examine_events_callback) {
    examine_events_callback(accessibility_pointer_events_);
  }

  // Setups a client in Scenic. All clients in this test are a 5x5, and only one
  // client is created per test.
  void SetupClient(scenic::Session* session, scenic::EntityNode* root_node, ViewToken v_token,
                   AccessibilityPointerEventsTest* test, bool start_listener = true) {
    // Connect our root node to the presenter's root node.
    auto pair = scenic::ViewRefPair::New();
    view_ref_koid_ = ExtractKoid(pair.view_ref);
    scenic::View view(session, std::move(v_token), std::move(pair.control_ref),
                      std::move(pair.view_ref), "View");
    view.AddChild(*root_node);

    scenic::ShapeNode shape(session);
    shape.SetTranslation(2, 2, 0);  // Center the shape within the View.
    root_node->AddChild(shape);

    scenic::Rectangle rec(session, 5, 5);  // Simple; no real GPU work.
    shape.SetShape(rec);

    scenic::Material material(session);
    shape.SetMaterial(material);
    if (start_listener) {
      SetupAccessibilityPointEventListener(test);
    }
    test->RequestToPresent(session);
  }

  // Configures how this fake class will answer to incoming events.
  //
  // |responses| is a vector, where each pair contains the number of events that
  // will be seen before it responds with an EventHandling value.
  void ConfigureResponses(
      std::vector<std::pair<uint32_t, fuchsia::ui::input::accessibility::EventHandling>>
          responses) {
    responses_ = std::move(responses);
  }

  zx_koid_t view_ref_koid() const { return view_ref_koid_; }

 private:
  // |fuchsia::ui::input::accessibility::AccessibilityPointerEventListener|
  // Perform the response, reset for next response.
  void OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event) override {
    accessibility_pointer_events_.emplace_back(std::move(pointer_event));
    ++num_events_until_response_;
    if (!responses_.empty() && num_events_until_response_ == responses_.front().first) {
      num_events_until_response_ = 0;
      listener_bindings_.bindings().front()->events().OnStreamHandled(
          /*device_id=*/1, /*pointer_id=*/1,
          /*handled=*/responses_.front().second);
      responses_.erase(responses_.begin());
    }
  }

  bool listener_registered_ = false;
  fidl::BindingSet<fuchsia::ui::input::accessibility::PointerEventListener> listener_bindings_;
  // Holds the number of AccessibilityPointerEvents that will be seen before a
  // response of type EventHandling will be received.
  std::vector<std::pair<uint32_t, fuchsia::ui::input::accessibility::EventHandling>> responses_;

  std::vector<fuchsia::ui::input::accessibility::PointerEvent> accessibility_pointer_events_;
  uint32_t num_events_until_response_ = 0;

  zx_koid_t view_ref_koid_ = ZX_KOID_INVALID;
};

TEST_F(AccessibilityPointerEventsTest, RegistersAccessibilityListenerOnlyOnce) {
  // This test makes sure that first to register win is working.
  SessionWrapper presenter(scenic());

  auto pair = scenic::ViewTokenPair::New();

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(pair.view_holder_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client_1(scenic());
  client_1.RunNow([this, &client_1, v_token = std::move(std::move(pair.view_token))](
                      scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client_1.SetupClient(session, root_node, std::move(v_token), this);
  });

  // Make sure that the listener was registered.
  client_1.ExamineEvents([&client_1](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
    EXPECT_TRUE(client_1.IsListenerRegistered());
  });

  // A second client attempts to connect and should fail, as there is already one connected.
  AccessibilityPointerEventListenerSessionWrapper client_2(scenic());
  client_2.RunNow(
      [this, &client_2](scenic::Session* session, scenic::EntityNode* root_node) mutable {
        client_2.SetupAccessibilityPointEventListener(this);  // For the second time, should fail.
        RequestToPresent(session);
      });

  client_2.ExamineEvents([&client_2](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
    EXPECT_FALSE(client_2.IsListenerRegistered());
  });

  // First client should be still connected.
  client_1.ExamineEvents([&client_1](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
    EXPECT_TRUE(client_1.IsListenerRegistered());
  });
}

TEST_F(AccessibilityPointerEventsTest, ConsumesPointerEvents) {
  // In this test two pointer event streams will be injected in the input
  // system. The first one, with four pointer events, will be accepted in the
  // second pointer event. The second one, also with four pointer events, will
  // be accepted in the fourth one.
  SessionWrapper presenter(scenic());

  auto pair = scenic::ViewTokenPair::New();

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(pair.view_holder_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(pair.view_token)](
                    scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.SetupClient(session, root_node, std::move(v_token), this);
    client.ConfigureResponses({{2, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
                               {6, fuchsia::ui::input::accessibility::EventHandling::CONSUMED}});
  });

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));  // Consume happens here.
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([&client](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 event.";
    EXPECT_TRUE(client.IsListenerRegistered());
  });

  // Verify client's accessibility pointer events.
  client.ExamineAccessibilityPointerEvents(
      [&client](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
          EXPECT_EQ(add.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(add.local_point().x, 2.5);
          EXPECT_EQ(add.local_point().y, 2.5);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 2);
          EXPECT_EQ(down.global_point().y, 2);
          EXPECT_EQ(down.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(down.local_point().x, 2.5);
          EXPECT_EQ(down.local_point().y, 2.5);
        }
      });

  client.ClearEvents();
  client.ClearAccessibilityEvents();

  // The client consumed the two events. continue sending pointer events in the
  // same stream (a phase == REMOVE hasn't came yet, so they are part of the
  // same stream).
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
  });

  // Verify client's accessibility pointer events.
  client.ExamineAccessibilityPointerEvents(
      [&client](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // UP
        {
          const AccessibilityPointerEvent& up = events[0];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 2);
          EXPECT_EQ(up.global_point().y, 3);
          EXPECT_EQ(up.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(up.local_point().x, 2.5);
          EXPECT_EQ(up.local_point().y, 3.5);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[1];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 2);
          EXPECT_EQ(remove.global_point().y, 3);
          EXPECT_EQ(remove.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(remove.local_point().x, 2.5);
          EXPECT_EQ(remove.local_point().y, 3.5);
        }
      });

  client.ClearEvents();
  client.ClearAccessibilityEvents();

  // Now, sends an entire stream at once.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(3, 1));
    session->Enqueue(pointer.Down(3, 1));
    session->Enqueue(pointer.Up(3, 1));
    session->Enqueue(pointer.Remove(3, 1));  // Consume happens here.
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
  });

  // Verify client's accessibility pointer events.
  client.ExamineAccessibilityPointerEvents(
      [&client](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 4u) << "Should receive exactly 4 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 3);
          EXPECT_EQ(add.global_point().y, 1);
          EXPECT_EQ(add.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(add.local_point().x, 3.5);
          EXPECT_EQ(add.local_point().y, 1.5);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 3);
          EXPECT_EQ(down.global_point().y, 1);
          EXPECT_EQ(down.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(down.local_point().x, 3.5);
          EXPECT_EQ(down.local_point().y, 1.5);
        }

        // UP
        {
          const AccessibilityPointerEvent& up = events[2];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 3);
          EXPECT_EQ(up.global_point().y, 1);
          EXPECT_EQ(up.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(up.local_point().x, 3.5);
          EXPECT_EQ(up.local_point().y, 1.5);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[3];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 3);
          EXPECT_EQ(remove.global_point().y, 1);
          EXPECT_EQ(remove.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(remove.local_point().x, 3.5);
          EXPECT_EQ(remove.local_point().y, 1.5);
        }
      });
}

TEST_F(AccessibilityPointerEventsTest, RejectsPointerEvents) {
  // One pointer stream is injected in the input system. The listener rejects
  // the pointer event. this test makes sure that buffered (past), as well as
  // future pointer events are sent to the client.
  SessionWrapper presenter(scenic());

  auto pair = scenic::ViewTokenPair::New();

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(pair.view_holder_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(pair.view_token)](
                    scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.SetupClient(session, root_node, std::move(v_token), this);
    client.ConfigureResponses({{2, fuchsia::ui::input::accessibility::EventHandling::REJECTED}});
  });

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));  // Reject happens here.
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([&client](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 3u) << "Should receive exactly 3 events.";

    // ADD
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& add = events[0].pointer();
      EXPECT_EQ(add.x, 2.5);
      EXPECT_EQ(add.y, 2.5);
    }

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());

    // DOWN
    {
      EXPECT_TRUE(events[2].is_pointer());
      const PointerEvent& down = events[2].pointer();
      EXPECT_EQ(down.x, 2.5);
      EXPECT_EQ(down.y, 2.5);
    }
    EXPECT_TRUE(client.IsListenerRegistered());
  });

  // Verify client's accessibility pointer events. Note that the listener must
  // see two events here, but not later, because it rejects the stream in the
  // second pointer event.
  client.ExamineAccessibilityPointerEvents(
      [&client](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
          EXPECT_EQ(add.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(add.local_point().x, 2.5);
          EXPECT_EQ(add.local_point().y, 2.5);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 2);
          EXPECT_EQ(down.global_point().y, 2);
          EXPECT_EQ(down.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(down.local_point().x, 2.5);
          EXPECT_EQ(down.local_point().y, 2.5);
        }
      });

  client.ClearEvents();
  client.ClearAccessibilityEvents();

  // Sends the rest of the stream.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
    // UP
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& up = events[0].pointer();
      EXPECT_EQ(up.x, 2.5);
      EXPECT_EQ(up.y, 3.5);
    }

    // REMOVE
    {
      EXPECT_TRUE(events[1].is_pointer());
      const PointerEvent& remove = events[1].pointer();
      EXPECT_EQ(remove.x, 2.5);
      EXPECT_EQ(remove.y, 3.5);
    }
  });

  client.ExamineAccessibilityPointerEvents(
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
      });
}

TEST_F(AccessibilityPointerEventsTest, AlternatingResponses) {
  // In this test three streams will be injected in the input system, where the
  // first will be consumed, the second rejected and the third also consumed.
  SessionWrapper presenter(scenic());

  auto pair = scenic::ViewTokenPair::New();

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(pair.view_holder_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(pair.view_token)](
                    scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.SetupClient(session, root_node, std::move(v_token), this);
    client.ConfigureResponses({{4, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
                               {4, fuchsia::ui::input::accessibility::EventHandling::REJECTED},
                               {4, fuchsia::ui::input::accessibility::EventHandling::CONSUMED}});
  });

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    // First stream:
    session->Enqueue(pointer.Add(1, 1));
    session->Enqueue(pointer.Down(1, 1));
    session->Enqueue(pointer.Up(1, 1));
    session->Enqueue(pointer.Remove(1, 1));  // Consume happens here.
    // Second stream:
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));
    session->Enqueue(pointer.Up(2, 2));
    session->Enqueue(pointer.Remove(2, 2));  // Reject happens here.
    // Third stream:
    session->Enqueue(pointer.Add(3, 3));
    session->Enqueue(pointer.Down(3, 3));
    session->Enqueue(pointer.Up(3, 3));
    session->Enqueue(pointer.Remove(3, 3));  // Consume happens here.
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  // Here, only the focus event and events from the second stream should be
  // present.
  client.ExamineEvents([&client](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 5u) << "Should receive exactly 5 event.";
    // ADD
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& add = events[0].pointer();
      EXPECT_EQ(add.x, 2.5);
      EXPECT_EQ(add.y, 2.5);
    }

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());

    // DOWN
    {
      EXPECT_TRUE(events[2].is_pointer());
      const PointerEvent& down = events[2].pointer();
      EXPECT_EQ(down.x, 2.5);
      EXPECT_EQ(down.y, 2.5);
    }

    // UP
    {
      EXPECT_TRUE(events[3].is_pointer());
      const PointerEvent& up = events[3].pointer();
      EXPECT_EQ(up.x, 2.5);
      EXPECT_EQ(up.y, 2.5);
    }

    // REMOVE
    {
      EXPECT_TRUE(events[4].is_pointer());
      const PointerEvent& remove = events[4].pointer();
      EXPECT_EQ(remove.x, 2.5);
      EXPECT_EQ(remove.y, 2.5);
    }
    EXPECT_TRUE(client.IsListenerRegistered());
  });

  // Verify client's accessibility pointer events.
  // The listener should see all events, as it is configured to see the entire
  // stream before consuming / rejecting it.
  client.ExamineAccessibilityPointerEvents(
      [&client](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 12u) << "Should receive exactly 12 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 1);
          EXPECT_EQ(add.global_point().y, 1);
          EXPECT_EQ(add.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(add.local_point().x, 1.5);
          EXPECT_EQ(add.local_point().y, 1.5);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 1);
          EXPECT_EQ(down.global_point().y, 1);
          EXPECT_EQ(down.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(down.local_point().x, 1.5);
          EXPECT_EQ(down.local_point().y, 1.5);
        }

        // UP
        {
          const AccessibilityPointerEvent& up = events[2];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 1);
          EXPECT_EQ(up.global_point().y, 1);
          EXPECT_EQ(up.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(up.local_point().x, 1.5);
          EXPECT_EQ(up.local_point().y, 1.5);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[3];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 1);
          EXPECT_EQ(remove.global_point().y, 1);
          EXPECT_EQ(remove.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(remove.local_point().x, 1.5);
          EXPECT_EQ(remove.local_point().y, 1.5);
        }

        // ADD
        {
          const AccessibilityPointerEvent& add = events[4];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
          EXPECT_EQ(add.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(add.local_point().x, 2.5);
          EXPECT_EQ(add.local_point().y, 2.5);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[5];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 2);
          EXPECT_EQ(down.global_point().y, 2);
          EXPECT_EQ(down.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(down.local_point().x, 2.5);
          EXPECT_EQ(down.local_point().y, 2.5);
        }

        // UP
        {
          const AccessibilityPointerEvent& up = events[6];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 2);
          EXPECT_EQ(up.global_point().y, 2);
          EXPECT_EQ(up.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(up.local_point().x, 2.5);
          EXPECT_EQ(up.local_point().y, 2.5);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[7];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 2);
          EXPECT_EQ(remove.global_point().y, 2);
          EXPECT_EQ(remove.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(remove.local_point().x, 2.5);
          EXPECT_EQ(remove.local_point().y, 2.5);
        }

        // ADD
        {
          const AccessibilityPointerEvent& add = events[8];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 3);
          EXPECT_EQ(add.global_point().y, 3);
          EXPECT_EQ(add.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(add.local_point().x, 3.5);
          EXPECT_EQ(add.local_point().y, 3.5);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[9];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 3);
          EXPECT_EQ(down.global_point().y, 3);
          EXPECT_EQ(down.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(down.local_point().x, 3.5);
          EXPECT_EQ(down.local_point().y, 3.5);
        }

        // UP
        {
          const AccessibilityPointerEvent& up = events[10];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 3);
          EXPECT_EQ(up.global_point().y, 3);
          EXPECT_EQ(up.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(up.local_point().x, 3.5);
          EXPECT_EQ(up.local_point().y, 3.5);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[11];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 3);
          EXPECT_EQ(remove.global_point().y, 3);
          EXPECT_EQ(remove.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(remove.local_point().x, 3.5);
          EXPECT_EQ(remove.local_point().y, 3.5);
        }
      });
}

TEST_F(AccessibilityPointerEventsTest, DiscardActiveStreamOnConnection) {
  // This test makes sure that if there is a stream in progress and the a11y
  // listener connects, the existing stream is not sent to the listener.
  SessionWrapper presenter(scenic());

  auto pair = scenic::ViewTokenPair::New();

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(pair.view_holder_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(pair.view_token)](
                    scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.SetupClient(session, root_node, std::move(v_token), this,
                       /*start_listener=*/false);
  });

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([&client](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 3u) << "Should receive exactly 3 events.";
    // ADD
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& add = events[0].pointer();
      EXPECT_EQ(add.x, 2.5);
      EXPECT_EQ(add.y, 2.5);
    }

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());

    // DOWN
    {
      EXPECT_TRUE(events[2].is_pointer());
      const PointerEvent& down = events[2].pointer();
      EXPECT_EQ(down.x, 2.5);
      EXPECT_EQ(down.y, 2.5);
    }
    EXPECT_FALSE(client.IsListenerRegistered());
  });

  // Verify client's accessibility pointer events.
  client.ExamineAccessibilityPointerEvents(
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
      });

  client.ClearEvents();

  // Now, connects the a11y listener in the middle of a stream.
  client.RunNow([this, &client](scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.SetupAccessibilityPointEventListener(this);
  });

  // Sends the rest of the stream.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([&client](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
    // UP
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& up = events[0].pointer();
      EXPECT_EQ(up.x, 2.5);
      EXPECT_EQ(up.y, 3.5);
    }

    // REMOVE
    {
      EXPECT_TRUE(events[1].is_pointer());
      const PointerEvent& remove = events[1].pointer();
      EXPECT_EQ(remove.x, 2.5);
      EXPECT_EQ(remove.y, 3.5);
    }
    EXPECT_TRUE(client.IsListenerRegistered());
  });

  client.ExamineAccessibilityPointerEvents(
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
      });
}

TEST_F(AccessibilityPointerEventsTest, DispatchEventsAfterDisconnection) {
  // This tests makes sure that if there is an active stream, and the a11y
  // disconnects, the stream is sent to regular clients.
  SessionWrapper presenter(scenic());

  auto pair = scenic::ViewTokenPair::New();

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(pair.view_holder_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(pair.view_token)](
                    scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.SetupClient(session, root_node, std::move(v_token), this);
  });

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([&client](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
    EXPECT_TRUE(client.IsListenerRegistered());
  });

  // Verify client's accessibility pointer events. Note that the listener must
  // see two events here, as it will disconnect just after.
  client.ExamineAccessibilityPointerEvents(
      [&client](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
          EXPECT_EQ(add.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(add.local_point().x, 2.5);
          EXPECT_EQ(add.local_point().y, 2.5);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 2);
          EXPECT_EQ(down.global_point().y, 2);
          EXPECT_EQ(down.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(down.local_point().x, 2.5);
          EXPECT_EQ(down.local_point().y, 2.5);
        }
      });

  client.ClearEvents();
  client.ClearAccessibilityEvents();

  // Disconnects  the a11y listener without answering what we are going to do
  // with the pointer events.
  client.RunNow([&client](scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.DisconnectListener();
  });

  // Sends the rest of the stream.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
    RunLoopUntilIdle();
  });

  // Verify client's regular events making sure that all pointer events are
  // there after the disconnection.
  client.ExamineEvents([&client](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 5u) << "Should receive exactly 5 events.";
    // ADD
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& add = events[0].pointer();
      EXPECT_EQ(add.x, 2.5);
      EXPECT_EQ(add.y, 2.5);
    }

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());

    // DOWN
    {
      EXPECT_TRUE(events[2].is_pointer());
      const PointerEvent& down = events[2].pointer();
      EXPECT_EQ(down.x, 2.5);
      EXPECT_EQ(down.y, 2.5);
    }

    // UP
    {
      EXPECT_TRUE(events[3].is_pointer());
      const PointerEvent& up = events[3].pointer();
      EXPECT_EQ(up.x, 2.5);
      EXPECT_EQ(up.y, 3.5);
    }

    // REMOVE
    {
      EXPECT_TRUE(events[4].is_pointer());
      const PointerEvent& remove = events[4].pointer();
      EXPECT_EQ(remove.x, 2.5);
      EXPECT_EQ(remove.y, 3.5);
    }
    EXPECT_FALSE(client.IsListenerRegistered());
  });

  client.ExamineAccessibilityPointerEvents(
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
      });
}

TEST_F(AccessibilityPointerEventsTest, FocusGetsSentAfterAddRejecting) {
  // One pointer stream is injected in the input system. The listener rejects
  // the pointer event after the ADD event. This test makes sure that the focus
  // event gets sent, even though the stream is no longer buffered and its
  // information is coming only from the active stream info data.
  SessionWrapper presenter(scenic());

  auto pair = scenic::ViewTokenPair::New();

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(pair.view_holder_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(pair.view_token)](
                    scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.SetupClient(session, root_node, std::move(v_token), this);
    client.ConfigureResponses({{1, fuchsia::ui::input::accessibility::EventHandling::REJECTED}});
  });

  // Scene is now set up, send in the input.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));  // Reject happens here.
    session->Enqueue(pointer.Down(2, 2));
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([&client](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 3u) << "Should receive exactly 3 events.";

    // ADD
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& add = events[0].pointer();
      EXPECT_EQ(add.x, 2.5);
      EXPECT_EQ(add.y, 2.5);
    }

    // FOCUS
    EXPECT_TRUE(events[1].is_focus());

    // DOWN
    {
      EXPECT_TRUE(events[2].is_pointer());
      const PointerEvent& down = events[2].pointer();
      EXPECT_EQ(down.x, 2.5);
      EXPECT_EQ(down.y, 2.5);
    }
    EXPECT_TRUE(client.IsListenerRegistered());
  });

  // Verify client's accessibility pointer events.
  client.ExamineAccessibilityPointerEvents(
      [&client](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
          EXPECT_EQ(add.viewref_koid(), client.view_ref_koid());
          EXPECT_EQ(add.local_point().x, 2.5);
          EXPECT_EQ(add.local_point().y, 2.5);
        }
      });

  client.ClearEvents();
  client.ClearAccessibilityEvents();

  // Sends the rest of the stream.
  presenter.RunNow([this, compositor_id](scenic::Session* session, scenic::EntityNode* root_node) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
    RunLoopUntilIdle();
  });

  // Verify client's regular events.
  client.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
    // UP
    {
      EXPECT_TRUE(events[0].is_pointer());
      const PointerEvent& up = events[0].pointer();
      EXPECT_EQ(up.x, 2.5);
      EXPECT_EQ(up.y, 3.5);
    }

    // REMOVE
    {
      EXPECT_TRUE(events[1].is_pointer());
      const PointerEvent& remove = events[1].pointer();
      EXPECT_EQ(remove.x, 2.5);
      EXPECT_EQ(remove.y, 3.5);
    }
  });

  client.ExamineAccessibilityPointerEvents(
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
      });
}

TEST_F(AccessibilityPointerEventsTest, ExposeTopMostViewRefKoid) {
  // In this test, there are 4 sessions: The presenter, the accessibility client, and two ordinary
  // clients, A and B. The presenter injects a pointer event stream onto A and B.  We alternate the
  // elevation of A and B; in each case, the topmost view's ViewRef KOID shold be observed.

  auto pair_a = scenic::ViewTokenPair::New();
  auto pair_b = scenic::ViewTokenPair::New();

  // Presenter sets up a scene with two views.
  struct Presenter : public SessionWrapper {
    Presenter(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::Compositor> compositor;
    std::unique_ptr<scenic::EntityNode> translation_a;
    std::unique_ptr<scenic::EntityNode> translation_b;
  } presenter(scenic());

  presenter.RunNow([test = this, &presenter, vh_a = std::move(pair_a.view_holder_token),
                    vh_b = std::move(pair_b.view_holder_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    // Minimal scene.
    presenter.compositor = std::make_unique<scenic::Compositor>(session);

    scenic::Scene scene(session);
    scenic::Camera camera(scene);
    scenic::Renderer renderer(session);
    renderer.SetCamera(camera);

    scenic::Layer layer(session);
    layer.SetSize(test->test_display_width_px(), test->test_display_height_px());
    layer.SetRenderer(renderer);

    scenic::LayerStack layer_stack(session);
    layer_stack.AddLayer(layer);
    presenter.compositor->SetLayerStack(layer_stack);

    scene.AddChild(*root_node);

    scenic::ViewHolder view_holder_a(session, std::move(vh_a), "View Holder A"),
        view_holder_b(session, std::move(vh_b), "View Holder B");

    // Create view bound for each view.
    const float kZero[3] = {0, 0, 0};
    view_holder_a.SetViewProperties(kZero, (float[3]){5, 5, 1}, kZero, kZero);
    view_holder_b.SetViewProperties(kZero, (float[3]){5, 5, 1}, kZero, kZero);

    // Create an entity node for each view to control elevation.
    presenter.translation_a = std::make_unique<scenic::EntityNode>(session);
    presenter.translation_a->SetTranslation(0, 0, 1);
    presenter.translation_a->Attach(view_holder_a);

    presenter.translation_b = std::make_unique<scenic::EntityNode>(session);
    presenter.translation_b->SetTranslation(0, 0, 2);  // B is lower than A.
    presenter.translation_b->Attach(view_holder_b);

    // Attach entity nodes to the root node.
    root_node->AddChild(*presenter.translation_a);
    root_node->AddChild(*presenter.translation_b);

    test->RequestToPresent(session);
  });

  // The accessibility client does not set up a view, but just listens.
  AccessibilityPointerEventListenerSessionWrapper a11y_client(scenic());
  a11y_client.RunNow(
      [test = this, &a11y_client](scenic::Session* session, scenic::EntityNode* root_node) mutable {
        a11y_client.SetupAccessibilityPointEventListener(test);
        test->RunLoopUntilIdle();

        EXPECT_TRUE(a11y_client.IsListenerRegistered());
      });

  // Clients set up their content.
  struct Client : public SessionWrapper {
    Client(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    void CreateScene(scenic::Session* session, ViewToken view_token,
                     scenic::EntityNode* root_node) {
      auto pair = scenic::ViewRefPair::New();
      view_ref_koid = ExtractKoid(pair.view_ref);
      view = std::make_unique<scenic::View>(session, std::move(view_token),
                                            std::move(pair.control_ref), std::move(pair.view_ref),
                                            "View");
      view->AddChild(*root_node);

      scenic::ShapeNode shape(session);
      shape.SetTranslation(2, 2, 0);  // Center the shape within the View.
      root_node->AddChild(shape);

      scenic::Rectangle rec(session, 5, 5);  // Simple; no real GPU work.
      shape.SetShape(rec);

      scenic::Material material(session);
      shape.SetMaterial(material);
    }
    std::unique_ptr<scenic::View> view;
    zx_koid_t view_ref_koid;
  } client_a(scenic()), client_b(scenic());

  client_a.RunNow([test = this, &client = client_a, v_token = std::move(pair_a.view_token)](
                      scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.CreateScene(session, std::move(v_token), root_node);
    test->RequestToPresent(session);
  });

  client_b.RunNow([test = this, &client = client_b, v_token = std::move(pair_b.view_token)](
                      scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client.CreateScene(session, std::move(v_token), root_node);
    test->RequestToPresent(session);
  });

#if 0
  FXL_LOG(INFO) << this->DumpScenes();  // Handy debugging.
#endif

  // Scene is now set up, send in the input.
  presenter.RunNow(
      [test = this, &presenter](scenic::Session* session, scenic::EntityNode* root_node) {
        PointerCommandGenerator pointer(presenter.compositor->id(), /*device id*/ 1,
                                        /*pointer id*/ 1, PointerEventType::TOUCH);
        // A touch sequence that starts at the (2,2) location of the 5x5 display.
        session->Enqueue(pointer.Add(2, 2));
        session->Enqueue(pointer.Down(2, 2));
        test->RunLoopUntilIdle();
      });

  // Verify clients' regular events.
  client_a.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 event.";
  });

  client_b.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 event.";
  });

  // Verify accessibility pointer events.
  a11y_client.ExamineAccessibilityPointerEvents(
      [&client_a](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
          EXPECT_EQ(add.viewref_koid(), client_a.view_ref_koid);
          EXPECT_EQ(add.local_point().x, 2.5);
          EXPECT_EQ(add.local_point().y, 2.5);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 2);
          EXPECT_EQ(down.global_point().y, 2);
          EXPECT_EQ(down.viewref_koid(), client_a.view_ref_koid);
          EXPECT_EQ(down.local_point().x, 2.5);
          EXPECT_EQ(down.local_point().y, 2.5);
        }
      });

  a11y_client.ClearAccessibilityEvents();

  // Raise B in elevation, higher than A.
  presenter.RunNow(
      [test = this, &presenter](scenic::Session* session, scenic::EntityNode* root_node) {
        presenter.translation_a->SetTranslation(0, 0, 2);
        presenter.translation_b->SetTranslation(0, 0, 1);  // B is higher than A.
        test->RequestToPresent(session);
      });

#if 0
  FXL_LOG(INFO) << this->DumpScenes();  // Handy debugging.
#endif

  // Scene is now set up, send in the input.
  presenter.RunNow(
      [test = this, &presenter](scenic::Session* session, scenic::EntityNode* root_node) {
        PointerCommandGenerator pointer(presenter.compositor->id(), /*device id*/ 1,
                                        /*pointer id*/ 1, PointerEventType::TOUCH);
        // A touch sequence that ends at the (1,3) location of the 5x5 display.
        session->Enqueue(pointer.Up(1, 3));
        session->Enqueue(pointer.Remove(1, 3));
        test->RunLoopUntilIdle();
      });

  // Verify clients' regular events.
  client_a.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 event.";
  });

  client_b.ExamineEvents([](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 event.";
  });

  // Verify accessibility pointer events.
  a11y_client.ExamineAccessibilityPointerEvents(
      [&client_b](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // UP
        {
          const AccessibilityPointerEvent& up = events[0];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 1);
          EXPECT_EQ(up.global_point().y, 3);
          EXPECT_EQ(up.viewref_koid(), client_b.view_ref_koid);
          EXPECT_EQ(up.local_point().x, 1.5);
          EXPECT_EQ(up.local_point().y, 3.5);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[1];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 1);
          EXPECT_EQ(remove.global_point().y, 3);
          EXPECT_EQ(remove.viewref_koid(), client_b.view_ref_koid);
          EXPECT_EQ(remove.local_point().x, 1.5);
          EXPECT_EQ(remove.local_point().y, 3.5);
        }
      });
}

}  // namespace lib_ui_input_tests
