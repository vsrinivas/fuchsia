// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <limits>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/input/tests/util.h"

namespace lib_ui_input_tests {
namespace {

// common test setups:
//
// In each test case, a basic Scenic scene will be created, as well as a client with a view. The
// test will also register an accessibility listener with the input system. Tests may then exercise
// the injection of pointer events into the session. Depending on the accessibility listener
// response, configured with client.SetResponses(...), the pointer events will be consumed /
// rejected. When they are consumed, the view should not receive any events. When they are
// rejected, it should.

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using InputCommand = fuchsia::ui::input::Command;
using Phase = fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::views::ViewHolderToken;
using fuchsia::ui::views::ViewToken;
using scenic_impl::gfx::ExtractKoid;

constexpr float kNdcEpsilon = std::numeric_limits<float>::epsilon();

class MockAccessibilityPointerEventListener
    : public fuchsia::ui::input::accessibility::PointerEventListener {
 public:
  MockAccessibilityPointerEventListener(scenic_impl::input::InputSystem* input) : binding_(this) {
    binding_.set_error_handler([this](zx_status_t) { is_registered_ = false; });
    input->Register(binding_.NewBinding(), [this](bool success) { is_registered_ = success; });
  }

  bool is_registered() const { return is_registered_; }
  std::vector<fuchsia::ui::input::accessibility::PointerEvent>& events() { return events_; }
  const std::vector<fuchsia::ui::input::accessibility::PointerEvent>& events() const {
    return events_;
  }

  // Configures how this mock will answer to incoming events.
  //
  // |responses| is a vector, where each pair contains the number of events that
  // will be seen before it responds with an EventHandling value.
  void SetResponses(
      std::vector<std::pair<uint32_t, fuchsia::ui::input::accessibility::EventHandling>>
          responses) {
    responses_ = std::move(responses);
  }

 private:
  // |fuchsia::ui::input::accessibility::AccessibilityPointerEventListener|
  // Performs a response, and resets for the next response.
  void OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event) override {
    events_.emplace_back(std::move(pointer_event));
    ++num_events_until_response_;
    if (!responses_.empty() && num_events_until_response_ == responses_.front().first) {
      num_events_until_response_ = 0;
      binding_.events().OnStreamHandled(
          /*device_id=*/1, /*pointer_id=*/1,
          /*handled=*/responses_.front().second);
      responses_.erase(responses_.begin());
    }
  }

  fidl::Binding<fuchsia::ui::input::accessibility::PointerEventListener> binding_;
  bool is_registered_ = false;
  // See |SetResponses|.
  std::vector<std::pair<uint32_t, fuchsia::ui::input::accessibility::EventHandling>> responses_;

  std::vector<fuchsia::ui::input::accessibility::PointerEvent> events_;
  uint32_t num_events_until_response_ = 0;
};

class AccessibilitySessionWrapper : public SessionWrapper {
 public:
  AccessibilitySessionWrapper(scenic_impl::Scenic* scenic, zx_koid_t viewref_koid)
      : SessionWrapper(scenic), viewref_koid_(viewref_koid) {}

  zx_koid_t viewref_koid() const { return viewref_koid_; }

 private:
  const zx_koid_t viewref_koid_;
};

// Setup common to most of the tests in this suite, which set up a single view.
struct SingleViewSetup {
  SessionWrapper root_session;
  AccessibilitySessionWrapper view;
  const uint32_t compositor_id;
};

// Test fixture that sets up a 5x5 "display" and has utilities to wire up views with view refs for
// Accessibility.
class AccessibilityPointerEventsTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }

  // Most of the tests in this suite set up a single view.
  SingleViewSetup SetUpSingleView(const fuchsia::ui::gfx::ViewProperties& view_properties) {
    auto [root_session, root_resources] = CreateScene();

    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    scenic::Session* const session = root_session.session();

    scenic::ViewHolder view_holder(session, std::move(view_holder_token), "View Holder");
    view_holder.SetViewProperties(view_properties);
    root_resources.scene.AddChild(view_holder);
    RequestToPresent(session);

    return {
        std::move(root_session),
        CreateClient(std::move(view_token)),
        root_resources.compositor.id(),
    };
  }

  // Sets up a client and captures its view ref KOID. For the most part, one client is created per
  // test.
  AccessibilitySessionWrapper CreateClient(ViewToken view_token) {
    auto [control_ref, view_ref] = scenic::ViewRefPair::New();
    AccessibilitySessionWrapper session_wrapper(scenic(), ExtractKoid(view_ref));
    scenic::View view(session_wrapper.session(), std::move(view_token), std::move(control_ref),
                      std::move(view_ref), "View");
    SetUpTestView(&view);
    return session_wrapper;
  }

 private:
};

// This test makes sure that first to register win is working.
TEST_F(AccessibilityPointerEventsTest, RegistersAccessibilityListenerOnlyOnce) {
  MockAccessibilityPointerEventListener listener_1(input_system());
  RunLoopUntilIdle();

  EXPECT_TRUE(listener_1.is_registered());

  MockAccessibilityPointerEventListener listener_2(input_system());
  RunLoopUntilIdle();

  EXPECT_FALSE(listener_2.is_registered()) << "The second listener that attempts to connect should "
                                              "fail, as there is already one connected.";
  EXPECT_TRUE(listener_1.is_registered()) << "First listener should still be connected.";
}

// In this test two pointer event streams will be injected in the input system. The first one, with
// four pointer events, will be accepted in the second pointer event. The second one, also with four
// pointer events, will be accepted in the fourth one.
TEST_F(AccessibilityPointerEventsTest, ConsumesPointerEvents) {
  auto [root_session, view, compositor_id] = SetUpSingleView(k5x5x1);

  MockAccessibilityPointerEventListener listener(input_system());
  listener.SetResponses({
      {2, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
      {6, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
  });

  // Scene is now set up; send in the input.
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));  // Consume happens here.
  }
  RunLoopUntilIdle();

  // Verify view's events.
  EXPECT_TRUE(view.events().empty())
      << "View should not receive events until Accessibility allows it.";

  // Verify accessibility's events.
  {
    const std::vector<AccessibilityPointerEvent>& events = listener.events();
    EXPECT_EQ(events.size(), 2u);
    // ADD
    {
      const AccessibilityPointerEvent& add = events[0];
      EXPECT_EQ(add.phase(), Phase::ADD);
      // A note on normalized coordinates: normalized coordinates are still subject to pixel jitter,
      // so the discrete [0, 5) becomes [-.8, .8]:
      //  0      1      2      3      4      5
      //      .5    1.5    2.5    3.5    4.5
      // -1  -.8    -.4     0      .4     .8 1
      EXPECT_EQ(add.ndc_point().x, 0);
      EXPECT_EQ(add.ndc_point().y, 0);
      EXPECT_EQ(add.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_EQ(down.ndc_point().x, 0);
      EXPECT_EQ(down.ndc_point().y, 0);
      EXPECT_EQ(down.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(down.local_point().x, 2.5);
      EXPECT_EQ(down.local_point().y, 2.5);
    }
  }

  view.events().clear();
  listener.events().clear();

  // Accessibility consumed the two events. Continue sending pointer events in the same stream (a
  // phase == REMOVE hasn't came yet, so they are part of the same stream).
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
  }
  RunLoopUntilIdle();

  // Verify view's events.
  EXPECT_TRUE(view.events().empty()) << "Accessibility should be consuming all events in this "
                                        "stream; view should not be seeing them.";

  // Verify accessibility's events.
  {
    const std::vector<AccessibilityPointerEvent>& events = listener.events();
    EXPECT_EQ(events.size(), 2u);
    // UP
    {
      const AccessibilityPointerEvent& up = events[0];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_EQ(up.ndc_point().x, 0);
      EXPECT_NEAR(up.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(up.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(up.local_point().x, 2.5);
      EXPECT_EQ(up.local_point().y, 3.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[1];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_EQ(remove.ndc_point().x, 0);
      EXPECT_NEAR(remove.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(remove.local_point().x, 2.5);
      EXPECT_EQ(remove.local_point().y, 3.5);
    }
  }

  view.events().clear();
  listener.events().clear();

  // Now, sends an entire stream at once.
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(3, 1));
    session->Enqueue(pointer.Down(3, 1));
    session->Enqueue(pointer.Up(3, 1));
    session->Enqueue(pointer.Remove(3, 1));  // Consume happens here.
  }
  RunLoopUntilIdle();

  // Verify view's events.
  EXPECT_TRUE(view.events().empty()) << "Accessibility should have consumed all events in the "
                                        "stream; view should not have seen them.";

  // Verify accessibility's events.
  {
    const std::vector<AccessibilityPointerEvent>& events = listener.events();
    EXPECT_EQ(events.size(), 4u);
    // ADD
    {
      const AccessibilityPointerEvent& add = events[0];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_NEAR(add.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(add.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(add.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(add.local_point().x, 3.5);
      EXPECT_EQ(add.local_point().y, 1.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_NEAR(down.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(down.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(down.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(down.local_point().x, 3.5);
      EXPECT_EQ(down.local_point().y, 1.5);
    }

    // UP
    {
      const AccessibilityPointerEvent& up = events[2];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_NEAR(up.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(up.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(up.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(up.local_point().x, 3.5);
      EXPECT_EQ(up.local_point().y, 1.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[3];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_NEAR(remove.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(remove.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(remove.local_point().x, 3.5);
      EXPECT_EQ(remove.local_point().y, 1.5);
    }
  }
}

// One pointer stream is injected in the input system. The listener rejects the pointer event. this
// test makes sure that buffered (past), as well as future pointer events are sent to the view.
TEST_F(AccessibilityPointerEventsTest, RejectsPointerEvents) {
  auto [root_session, view, compositor_id] = SetUpSingleView(k5x5x1);

  MockAccessibilityPointerEventListener listener(input_system());
  listener.SetResponses({{2, fuchsia::ui::input::accessibility::EventHandling::REJECTED}});

  // Scene is now set up; send in the input.
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));  // Reject happens here.
  }
  RunLoopUntilIdle();

  // Verify view's events.
  {
    const std::vector<InputEvent>& events = view.events();
    EXPECT_EQ(events.size(), 3u);

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
  }

  // Verify accessibility's events. Note that the listener must see two events here, but not later,
  // because it rejects the stream in the second pointer event.
  {
    const std::vector<AccessibilityPointerEvent>& events = listener.events();
    EXPECT_EQ(events.size(), 2u);
    // ADD
    {
      const AccessibilityPointerEvent& add = events[0];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_EQ(add.ndc_point().x, 0);
      EXPECT_EQ(add.ndc_point().y, 0);
      EXPECT_EQ(add.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_EQ(down.ndc_point().x, 0);
      EXPECT_EQ(down.ndc_point().y, 0);
      EXPECT_EQ(down.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(down.local_point().x, 2.5);
      EXPECT_EQ(down.local_point().y, 2.5);
    }
  }

  view.events().clear();
  listener.events().clear();

  // Send the rest of the stream.
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
  }
  RunLoopUntilIdle();

  // Verify view's events.
  {
    const std::vector<InputEvent>& events = view.events();
    EXPECT_EQ(events.size(), 2u);
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
  }

  EXPECT_TRUE(listener.events().empty())
      << "Accessibility should stop receiving events in a stream after rejecting it.";
}

// In this test three streams will be injected in the input system, where the first will be
// consumed, the second rejected and the third also consumed.
TEST_F(AccessibilityPointerEventsTest, AlternatingResponses) {
  auto [root_session, view, compositor_id] = SetUpSingleView(k5x5x1);

  MockAccessibilityPointerEventListener listener(input_system());
  listener.SetResponses({
      {4, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
      {4, fuchsia::ui::input::accessibility::EventHandling::REJECTED},
      {4, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
  });

  // Scene is now set up; send in the input.
  {
    scenic::Session* const session = root_session.session();
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
  }
  RunLoopUntilIdle();

  // Verify view's events.
  // Here, only the focus event and events from the second stream should be present.
  {
    const std::vector<InputEvent>& events = view.events();
    EXPECT_EQ(events.size(), 5u);
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
  }

  // Verify accessibility's events.
  // The listener should see all events, as it is configured to see the entire stream before
  // consuming / rejecting it.
  {
    const std::vector<AccessibilityPointerEvent>& events = listener.events();
    EXPECT_EQ(events.size(), 12u);
    // ADD
    {
      const AccessibilityPointerEvent& add = events[0];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_NEAR(add.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(add.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(add.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(add.local_point().x, 1.5);
      EXPECT_EQ(add.local_point().y, 1.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_NEAR(down.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(down.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(down.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(down.local_point().x, 1.5);
      EXPECT_EQ(down.local_point().y, 1.5);
    }

    // UP
    {
      const AccessibilityPointerEvent& up = events[2];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_NEAR(up.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(up.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(up.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(up.local_point().x, 1.5);
      EXPECT_EQ(up.local_point().y, 1.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[3];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_NEAR(remove.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(remove.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(remove.local_point().x, 1.5);
      EXPECT_EQ(remove.local_point().y, 1.5);
    }

    // ADD
    {
      const AccessibilityPointerEvent& add = events[4];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_EQ(add.ndc_point().x, 0);
      EXPECT_EQ(add.ndc_point().y, 0);
      EXPECT_EQ(add.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[5];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_EQ(down.ndc_point().x, 0);
      EXPECT_EQ(down.ndc_point().y, 0);
      EXPECT_EQ(down.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(down.local_point().x, 2.5);
      EXPECT_EQ(down.local_point().y, 2.5);
    }

    // UP
    {
      const AccessibilityPointerEvent& up = events[6];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_EQ(up.ndc_point().x, 0);
      EXPECT_EQ(up.ndc_point().y, 0);
      EXPECT_EQ(up.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(up.local_point().x, 2.5);
      EXPECT_EQ(up.local_point().y, 2.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[7];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_EQ(remove.ndc_point().x, 0);
      EXPECT_EQ(remove.ndc_point().y, 0);
      EXPECT_EQ(remove.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(remove.local_point().x, 2.5);
      EXPECT_EQ(remove.local_point().y, 2.5);
    }

    // ADD
    {
      const AccessibilityPointerEvent& add = events[8];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_NEAR(add.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(add.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(add.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(add.local_point().x, 3.5);
      EXPECT_EQ(add.local_point().y, 3.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[9];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_NEAR(down.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(down.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(down.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(down.local_point().x, 3.5);
      EXPECT_EQ(down.local_point().y, 3.5);
    }

    // UP
    {
      const AccessibilityPointerEvent& up = events[10];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_NEAR(up.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(up.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(up.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(up.local_point().x, 3.5);
      EXPECT_EQ(up.local_point().y, 3.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[11];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_NEAR(remove.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(remove.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(remove.local_point().x, 3.5);
      EXPECT_EQ(remove.local_point().y, 3.5);
    }
  }

  // Make sure we didn't disconnect at some point for some reason.
  EXPECT_TRUE(listener.is_registered());
}

// This test makes sure that if there is a stream in progress and the accessibility listener
// connects, the existing stream is not sent to the listener.
TEST_F(AccessibilityPointerEventsTest, DiscardActiveStreamOnConnection) {
  auto [root_session, view, compositor_id] = SetUpSingleView(k5x5x1);

  // Scene is now set up, send in the input.
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));
  }
  RunLoopUntilIdle();

  // Verify view's events.
  EXPECT_EQ(view.events().size(), 3u);

  view.events().clear();

  // Now, connect the accessibility listener in the middle of a stream.
  MockAccessibilityPointerEventListener listener(input_system());

  // Sends the rest of the stream.
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
  }
  RunLoopUntilIdle();

  // Verify view's events.
  {
    const std::vector<InputEvent>& events = view.events();
    EXPECT_EQ(events.size(), 2u);
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
  }

  EXPECT_TRUE(listener.is_registered());
  EXPECT_TRUE(listener.events().empty()) << "Accessibility should not receive events from a stream "
                                            "already in progress when it was registered.";
}

// This tests makes sure that if there is an active stream, and accessibility disconnects, the
// stream is sent to regular clients.
TEST_F(AccessibilityPointerEventsTest, DispatchEventsAfterDisconnection) {
  auto [root_session, view, compositor_id] = SetUpSingleView(k5x5x1);

  {
    MockAccessibilityPointerEventListener listener(input_system());

    // Scene is now set up; send in the input.
    {
      scenic::Session* const session = root_session.session();
      PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                      /*pointer id*/ 1, PointerEventType::TOUCH);
      // A touch sequence that starts at the (2,2) location of the 5x5 display.
      session->Enqueue(pointer.Add(2, 2));
      session->Enqueue(pointer.Down(2, 2));
    }
    RunLoopUntilIdle();

    // Verify view's events.
    EXPECT_TRUE(view.events().empty());

    // Verify client's accessibility pointer events. Note that the listener must
    // see two events here, as it will disconnect just after.
    EXPECT_EQ(listener.events().size(), 2u);

    // Let the accessibility listener go out of scope without answering what we are going to do with
    // the pointer events.
  }
  view.events().clear();

  // Sends the rest of the stream.
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
  }
  RunLoopUntilIdle();

  // Verify that all pointer events get routed to the view after disconnection.
  {
    const std::vector<InputEvent>& events = view.events();
    EXPECT_EQ(events.size(), 5u);
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
  }
}

// One pointer stream is injected in the input system. The listener rejects the pointer event after
// the ADD event. This test makes sure that the focus event gets sent, even though the stream is no
// longer buffered and its information is coming only from the active stream info data.
TEST_F(AccessibilityPointerEventsTest, FocusGetsSentAfterAddRejecting) {
  auto [root_session, view, compositor_id] = SetUpSingleView(k5x5x1);

  MockAccessibilityPointerEventListener listener(input_system());
  listener.SetResponses({{1, fuchsia::ui::input::accessibility::EventHandling::REJECTED}});

  // Scene is now set up; send in the input.
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));  // Reject happens here.
    session->Enqueue(pointer.Down(2, 2));
  }
  RunLoopUntilIdle();

  // Verify view's events.
  {
    const std::vector<InputEvent>& events = view.events();
    EXPECT_EQ(events.size(), 3u);

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
  }

  // Verify client's accessibility pointer events.
  {
    const std::vector<AccessibilityPointerEvent>& events = listener.events();
    EXPECT_EQ(events.size(), 2u);
    // ADD
    {
      const AccessibilityPointerEvent& add = events[0];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_EQ(add.ndc_point().x, 0);
      EXPECT_EQ(add.ndc_point().y, 0);
      EXPECT_EQ(add.viewref_koid(), view.viewref_koid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }
    // TODO(rosswang): What's the second one?
  }

  view.events().clear();
  listener.events().clear();

  // Sends the rest of the stream.
  {
    scenic::Session* const session = root_session.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2, 3));
    session->Enqueue(pointer.Remove(2, 3));
  }
  RunLoopUntilIdle();

  // Verify view's events.
  {
    const std::vector<InputEvent>& events = view.events();
    EXPECT_EQ(events.size(), 2u);
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
  }

  EXPECT_TRUE(listener.events().empty());
}

// In this test, there are two views. The root session injects a pointer event stream onto both. We
// alternate the elevation of the views; in each case, the topmost view's ViewRef KOID shold be
// observed.
TEST_F(AccessibilityPointerEventsTest, ExposeTopMostViewRefKoid) {
  MockAccessibilityPointerEventListener listener(input_system());

  auto [v_a, vh_a] = scenic::ViewTokenPair::New();
  auto [v_b, vh_b] = scenic::ViewTokenPair::New();

  // Set up a scene with two views.
  // Since we need to manipulate the scene graph, go ahead and do this all at function scope.
  auto [root_session, root_resources] = CreateScene();
  scenic::Session* const session = root_session.session();
  scenic::Scene* const scene = &root_resources.scene;

  scenic::ViewHolder view_holder_a(session, std::move(vh_a), "View Holder A"),
      view_holder_b(session, std::move(vh_b), "View Holder B");

  view_holder_a.SetViewProperties(k5x5x1);
  view_holder_b.SetViewProperties(k5x5x1);

  // Translate each view to control elevation.
  view_holder_a.SetTranslation(0, 0, 1);
  view_holder_b.SetTranslation(0, 0, 2);  // B is lower than A.

  // Attach views to the scene.
  scene->AddChild(view_holder_a);
  scene->AddChild(view_holder_b);

  RequestToPresent(session);

  AccessibilitySessionWrapper view_a = CreateClient(std::move(v_a)),
                              view_b = CreateClient(std::move(v_b));

  const uint32_t compositor_id = root_resources.compositor.id();

  // Scene is now set up; send in the input.
  {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2,2) location of the 5x5 display.
    session->Enqueue(pointer.Add(2, 2));
    session->Enqueue(pointer.Down(2, 2));
  }
  RunLoopUntilIdle();

  // Verify views' events.
  EXPECT_TRUE(view_a.events().empty());
  EXPECT_TRUE(view_b.events().empty());

  // Verify accessibility's events.
  {
    const std::vector<AccessibilityPointerEvent>& events = listener.events();
    EXPECT_EQ(events.size(), 2u);
    // ADD
    {
      const AccessibilityPointerEvent& add = events[0];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_EQ(add.ndc_point().x, 0);
      EXPECT_EQ(add.ndc_point().y, 0);
      EXPECT_EQ(add.viewref_koid(), view_a.viewref_koid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_EQ(down.ndc_point().x, 0);
      EXPECT_EQ(down.ndc_point().y, 0);
      EXPECT_EQ(down.viewref_koid(), view_a.viewref_koid());
      EXPECT_EQ(down.local_point().x, 2.5);
      EXPECT_EQ(down.local_point().y, 2.5);
    }
  }

  view_a.events().clear();
  view_b.events().clear();
  listener.events().clear();

  // Raise B in elevation, higher than A.
  view_holder_a.SetTranslation(0, 0, 2);
  view_holder_b.SetTranslation(0, 0, 1);  // B is higher than A.
  RequestToPresent(session);

  // Scene is now set up, send in the input.
  {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that ends at the (1,3) location of the 5x5 display.
    session->Enqueue(pointer.Up(1, 3));
    session->Enqueue(pointer.Remove(1, 3));
  }
  RunLoopUntilIdle();

  // Verify views' events.
  EXPECT_TRUE(view_a.events().empty());
  EXPECT_TRUE(view_b.events().empty());

  // Verify accessibility's events.
  {
    const std::vector<AccessibilityPointerEvent>& events = listener.events();
    EXPECT_EQ(events.size(), 2u);
    // UP
    {
      const AccessibilityPointerEvent& up = events[0];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_NEAR(up.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(up.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(up.viewref_koid(), view_b.viewref_koid());
      EXPECT_EQ(up.local_point().x, 1.5);
      EXPECT_EQ(up.local_point().y, 3.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[1];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_NEAR(remove.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(remove.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view_b.viewref_koid());
      EXPECT_EQ(remove.local_point().x, 1.5);
      EXPECT_EQ(remove.local_point().y, 3.5);
    }
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
