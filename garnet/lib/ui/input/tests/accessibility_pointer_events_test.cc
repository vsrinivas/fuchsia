// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/zx/clock.h>
#include <lib/zx/eventpair.h>

#include "garnet/lib/ui/input/tests/util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"
#include "src/lib/fxl/logging.h"

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

using fuchsia::ui::input::InputEvent;
using InputCommand = fuchsia::ui::input::Command;
using fuchsia::ui::input::PointerEvent;
using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

// Class fixture for TEST_F. Sets up a 5x5 "display" for GfxSystem.
class AccessibilityPointerEventsTest : public InputSystemTest {
 public:
  // Sets up a minimal scene.
  void SetupScene(scenic::Session* session, scenic::EntityNode* root_node, uint32_t* compositor_id,
                  zx::eventpair vh_token) {
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
    const float bbox_min[3] = {0, 0, 0};
    const float bbox_max[3] = {5, 5, 1};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    view_holder.SetViewProperties(bbox_min, bbox_max, inset_min, inset_max);
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
  void SetupClient(scenic::Session* session, scenic::EntityNode* root_node, zx::eventpair v_token,
                   AccessibilityPointerEventsTest* test, bool start_listener = true) {
    // Connect our root node to the presenter's root node.
    scenic::View view(session, std::move(v_token), "View");
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

 private:
  // |fuchsia::ui::input::accessibility::AccessibilityPointerEventListener|
  // Perform the response, reset for next response.
  void OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event,
               OnEventCallback callback) override {
    accessibility_pointer_events_.emplace_back(std::move(pointer_event));
    ++num_events_until_response_;
    if (!responses_.empty() && num_events_until_response_ == responses_.front().first) {
      num_events_until_response_ = 0;
      callback(/*device_id=*/1, /*pointer_id=*/1,
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
};

TEST_F(AccessibilityPointerEventsTest, RegistersAccessibilityListenerOnlyOnce) {
  // This test makes sure that first to register win is working.
  SessionWrapper presenter(scenic());

  zx::eventpair v_token, vh_token;
  CreateTokenPair(&v_token, &vh_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(vh_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client_1(scenic());
  client_1.RunNow([this, &client_1, v_token = std::move(v_token)](
                      scenic::Session* session, scenic::EntityNode* root_node) mutable {
    client_1.SetupClient(session, root_node, std::move(v_token), this);
  });

  // Make sure that the listener was registered.
  client_1.ExamineEvents([&client_1](const std::vector<InputEvent>& events) {
    EXPECT_EQ(events.size(), 0u) << "Should receive exactly 0 events.";
    EXPECT_TRUE(client_1.IsListenerRegistered());
  });

  // A second client atemps to connect and should fail, as there is already one
  // connected.
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

  zx::eventpair v_token, vh_token;
  CreateTokenPair(&v_token, &vh_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(vh_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(v_token)](
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
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 2);
          EXPECT_EQ(down.global_point().y, 2);
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
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // UP
        {
          const AccessibilityPointerEvent& up = events[0];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 2);
          EXPECT_EQ(up.global_point().y, 3);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[1];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 2);
          EXPECT_EQ(remove.global_point().y, 3);
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
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 4u) << "Should receive exactly 4 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 3);
          EXPECT_EQ(add.global_point().y, 1);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 3);
          EXPECT_EQ(down.global_point().y, 1);
        }

        // UP
        {
          const AccessibilityPointerEvent& up = events[2];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 3);
          EXPECT_EQ(up.global_point().y, 1);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[3];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 3);
          EXPECT_EQ(remove.global_point().y, 1);
        }
      });
}

TEST_F(AccessibilityPointerEventsTest, RejectsPointerEvents) {
  // One pointer stream is injected in the input system. The listener rejects
  // the pointer event. this test makes sure that buffered (past), as well as
  // future pointer events are sent to the client.
  SessionWrapper presenter(scenic());

  zx::eventpair v_token, vh_token;
  CreateTokenPair(&v_token, &vh_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(vh_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(v_token)](
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
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 2);
          EXPECT_EQ(down.global_point().y, 2);
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

  zx::eventpair v_token, vh_token;
  CreateTokenPair(&v_token, &vh_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(vh_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(v_token)](
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
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 12u) << "Should receive exactly 12 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 1);
          EXPECT_EQ(add.global_point().y, 1);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 1);
          EXPECT_EQ(down.global_point().y, 1);
        }

        // UP
        {
          const AccessibilityPointerEvent& up = events[2];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 1);
          EXPECT_EQ(up.global_point().y, 1);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[3];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 1);
          EXPECT_EQ(remove.global_point().y, 1);
        }

        // ADD
        {
          const AccessibilityPointerEvent& add = events[4];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[5];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 2);
          EXPECT_EQ(down.global_point().y, 2);
        }

        // UP
        {
          const AccessibilityPointerEvent& up = events[6];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 2);
          EXPECT_EQ(up.global_point().y, 2);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[7];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 2);
          EXPECT_EQ(remove.global_point().y, 2);
        }

        // ADD
        {
          const AccessibilityPointerEvent& add = events[8];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 3);
          EXPECT_EQ(add.global_point().y, 3);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[9];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 3);
          EXPECT_EQ(down.global_point().y, 3);
        }

        // UP
        {
          const AccessibilityPointerEvent& up = events[10];
          EXPECT_EQ(up.phase(), Phase::UP);
          EXPECT_EQ(up.global_point().x, 3);
          EXPECT_EQ(up.global_point().y, 3);
        }

        // REMOVE
        {
          const AccessibilityPointerEvent& remove = events[11];
          EXPECT_EQ(remove.phase(), Phase::REMOVE);
          EXPECT_EQ(remove.global_point().x, 3);
          EXPECT_EQ(remove.global_point().y, 3);
        }
      });
}

TEST_F(AccessibilityPointerEventsTest, DiscardActiveStreamOnConnection) {
  // This test makes sure that if there is a stream in progress and the a11y
  // listener connects, the existing stream is not sent to the listener.
  SessionWrapper presenter(scenic());

  zx::eventpair v_token, vh_token;
  CreateTokenPair(&v_token, &vh_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(vh_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(v_token)](
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

  zx::eventpair v_token, vh_token;
  CreateTokenPair(&v_token, &vh_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(vh_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(v_token)](
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
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
        }

        // DOWN
        {
          const AccessibilityPointerEvent& down = events[1];
          EXPECT_EQ(down.phase(), Phase::DOWN);
          EXPECT_EQ(down.global_point().x, 2);
          EXPECT_EQ(down.global_point().y, 2);
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

  zx::eventpair v_token, vh_token;
  CreateTokenPair(&v_token, &vh_token);

  // Tie the test's dispatcher clock to the system (real) clock.
  RunLoopUntil(zx::clock::get_monotonic());

  // "Presenter" sets up a scene with one view.
  uint32_t compositor_id = 0;
  presenter.RunNow([this, &compositor_id, vh_token = std::move(vh_token)](
                       scenic::Session* session, scenic::EntityNode* root_node) mutable {
    SetupScene(session, root_node, &compositor_id, std::move(vh_token));
  });

  // Client sets up its content.
  AccessibilityPointerEventListenerSessionWrapper client(scenic());
  client.RunNow([this, &client, v_token = std::move(v_token)](
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
      [](const std::vector<AccessibilityPointerEvent>& events) {
        EXPECT_EQ(events.size(), 2u) << "Should receive exactly 2 events.";
        // ADD
        {
          const AccessibilityPointerEvent& add = events[0];
          EXPECT_EQ(add.phase(), Phase::ADD);
          EXPECT_EQ(add.global_point().x, 2);
          EXPECT_EQ(add.global_point().y, 2);
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

}  // namespace lib_ui_input_tests
