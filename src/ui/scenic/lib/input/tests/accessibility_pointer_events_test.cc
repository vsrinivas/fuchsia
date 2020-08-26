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
#include "src/ui/scenic/lib/utils/helpers.h"

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
using scenic_impl::gfx::ViewTree;

constexpr float kNdcEpsilon = std::numeric_limits<float>::epsilon();

class MockAccessibilityPointerEventListener
    : public fuchsia::ui::input::accessibility::PointerEventListener {
 public:
  MockAccessibilityPointerEventListener(scenic_impl::input::InputSystem* input) : binding_(this) {
    binding_.set_error_handler([this](zx_status_t) { is_registered_ = false; });
    input->RegisterA11yListener(binding_.NewBinding(),
                                [this](bool success) { is_registered_ = success; });
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

// Setup common to most of the tests in this suite, which set up a single child view.
struct SingleChildViewSetup {
  SessionWrapper root_view;
  SessionWrapper child_view;
  const uint32_t compositor_id;
};

// Setup with two nested child views, for testing injection that requires context and target views.
struct TwoChildViewSetup {
  SessionWrapper root_view;
  SessionWrapper child_view;
  SessionWrapper child_view2;
  const uint32_t compositor_id;
};

// Test fixture that sets up a 5x5 "display" and has utilities to wire up views with view refs for
// Accessibility.
class AccessibilityPointerEventsTest : public InputSystemTest {
 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }

  // Most of the tests in this suite set up a single view.
  SingleChildViewSetup SetUpSingleView(const fuchsia::ui::gfx::ViewProperties& view_properties) {
    auto [root_view, root_resources] = CreateScene();
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    {
      scenic::ViewHolder view_holder(root_view.session(), std::move(view_holder_token),
                                     "View Holder");
      view_holder.SetViewProperties(view_properties);
      root_view.view()->AddChild(view_holder);
    }

    RequestToPresent(root_view.session());

    return {
        std::move(root_view),
        CreateClient("a11y-single-view", std::move(view_token)),
        root_resources.compositor.id(),
    };
  }

  TwoChildViewSetup SetUpTwoViews(const fuchsia::ui::gfx::ViewProperties& view_properties) {
    SingleChildViewSetup single_view_scene = SetUpSingleView(view_properties);

    auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();
    {
      scenic::ViewHolder view_holder(single_view_scene.child_view.session(),
                                     std::move(view_holder_token2), "View Holder");
      view_holder.SetViewProperties(view_properties);
      single_view_scene.child_view.view()->AddChild(view_holder);
    }

    RequestToPresent(single_view_scene.child_view.session());

    return {
        .root_view = std::move(single_view_scene.root_view),
        .child_view = std::move(single_view_scene.child_view),
        .child_view2 = CreateClient("a11y-second-view", std::move(view_token2)),
        .compositor_id = single_view_scene.compositor_id,
    };
  }
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
  auto [root_view, view, compositor_id] = SetUpSingleView(k5x5x1);

  MockAccessibilityPointerEventListener listener(input_system());
  listener.SetResponses({
      {2, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
      {6, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
  });

  // Scene is now set up; send in the input.
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2.5,2.5) location of the 5x5 display.
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));  // Consume happens here.
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
      EXPECT_EQ(add.ndc_point().x, 0);
      EXPECT_EQ(add.ndc_point().y, 0);
      EXPECT_EQ(add.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_EQ(down.ndc_point().x, 0);
      EXPECT_EQ(down.ndc_point().y, 0);
      EXPECT_EQ(down.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(down.local_point().x, 2.5);
      EXPECT_EQ(down.local_point().y, 2.5);
    }
  }

  view.events().clear();
  listener.events().clear();

  // Accessibility consumed the two events. Continue sending pointer events in the same stream (a
  // phase == REMOVE hasn't came yet, so they are part of the same stream).
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2.5, 3.5));
    session->Enqueue(pointer.Remove(2.5, 3.5));
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
      EXPECT_EQ(up.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(up.local_point().x, 2.5);
      EXPECT_EQ(up.local_point().y, 3.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[1];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_EQ(remove.ndc_point().x, 0);
      EXPECT_NEAR(remove.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(remove.local_point().x, 2.5);
      EXPECT_EQ(remove.local_point().y, 3.5);
    }
  }

  view.events().clear();
  listener.events().clear();

  // Now, sends an entire stream at once.
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Add(3.5, 1.5));
    session->Enqueue(pointer.Down(3.5, 1.5));
    session->Enqueue(pointer.Up(3.5, 1.5));
    session->Enqueue(pointer.Remove(3.5, 1.5));  // Consume happens here.
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
      EXPECT_EQ(add.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(add.local_point().x, 3.5);
      EXPECT_EQ(add.local_point().y, 1.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_NEAR(down.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(down.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(down.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(down.local_point().x, 3.5);
      EXPECT_EQ(down.local_point().y, 1.5);
    }

    // UP
    {
      const AccessibilityPointerEvent& up = events[2];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_NEAR(up.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(up.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(up.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(up.local_point().x, 3.5);
      EXPECT_EQ(up.local_point().y, 1.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[3];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_NEAR(remove.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(remove.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(remove.local_point().x, 3.5);
      EXPECT_EQ(remove.local_point().y, 1.5);
    }
  }
}

// One pointer stream is injected in the input system. The listener rejects the pointer event. this
// test makes sure that buffered (past), as well as future pointer events are sent to the view.
TEST_F(AccessibilityPointerEventsTest, RejectsPointerEvents) {
  auto [root_view, view, compositor_id] = SetUpSingleView(k5x5x1);

  MockAccessibilityPointerEventListener listener(input_system());
  listener.SetResponses({{2, fuchsia::ui::input::accessibility::EventHandling::REJECTED}});

  // Scene is now set up; send in the input.
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2.5,2.5) location of the 5x5 display.
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));  // Reject happens here.
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
      EXPECT_EQ(add.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_EQ(down.ndc_point().x, 0);
      EXPECT_EQ(down.ndc_point().y, 0);
      EXPECT_EQ(down.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(down.local_point().x, 2.5);
      EXPECT_EQ(down.local_point().y, 2.5);
    }
  }

  view.events().clear();
  listener.events().clear();

  // Send the rest of the stream.
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2.5, 3.5));
    session->Enqueue(pointer.Remove(2.5, 3.5));
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
  auto [root_view, view, compositor_id] = SetUpSingleView(k5x5x1);

  MockAccessibilityPointerEventListener listener(input_system());
  listener.SetResponses({
      {4, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
      {4, fuchsia::ui::input::accessibility::EventHandling::REJECTED},
      {4, fuchsia::ui::input::accessibility::EventHandling::CONSUMED},
  });

  // Scene is now set up; send in the input.
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2.5,2.5) location of the 5x5 display.
    // First stream:
    session->Enqueue(pointer.Add(1.5, 1.5));
    session->Enqueue(pointer.Down(1.5, 1.5));
    session->Enqueue(pointer.Up(1.5, 1.5));
    session->Enqueue(pointer.Remove(1.5, 1.5));  // Consume happens here.
    // Second stream:
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));
    session->Enqueue(pointer.Up(2.5, 2.5));
    session->Enqueue(pointer.Remove(2.5, 2.5));  // Reject happens here.
    // Third stream:
    session->Enqueue(pointer.Add(3.5, 3.5));
    session->Enqueue(pointer.Down(3.5, 3.5));
    session->Enqueue(pointer.Up(3.5, 3.5));
    session->Enqueue(pointer.Remove(3.5, 3.5));  // Consume happens here.
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
      EXPECT_EQ(add.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(add.local_point().x, 1.5);
      EXPECT_EQ(add.local_point().y, 1.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_NEAR(down.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(down.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(down.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(down.local_point().x, 1.5);
      EXPECT_EQ(down.local_point().y, 1.5);
    }

    // UP
    {
      const AccessibilityPointerEvent& up = events[2];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_NEAR(up.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(up.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(up.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(up.local_point().x, 1.5);
      EXPECT_EQ(up.local_point().y, 1.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[3];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_NEAR(remove.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(remove.ndc_point().y, -.4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(remove.local_point().x, 1.5);
      EXPECT_EQ(remove.local_point().y, 1.5);
    }

    // ADD
    {
      const AccessibilityPointerEvent& add = events[4];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_EQ(add.ndc_point().x, 0);
      EXPECT_EQ(add.ndc_point().y, 0);
      EXPECT_EQ(add.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[5];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_EQ(down.ndc_point().x, 0);
      EXPECT_EQ(down.ndc_point().y, 0);
      EXPECT_EQ(down.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(down.local_point().x, 2.5);
      EXPECT_EQ(down.local_point().y, 2.5);
    }

    // UP
    {
      const AccessibilityPointerEvent& up = events[6];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_EQ(up.ndc_point().x, 0);
      EXPECT_EQ(up.ndc_point().y, 0);
      EXPECT_EQ(up.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(up.local_point().x, 2.5);
      EXPECT_EQ(up.local_point().y, 2.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[7];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_EQ(remove.ndc_point().x, 0);
      EXPECT_EQ(remove.ndc_point().y, 0);
      EXPECT_EQ(remove.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(remove.local_point().x, 2.5);
      EXPECT_EQ(remove.local_point().y, 2.5);
    }

    // ADD
    {
      const AccessibilityPointerEvent& add = events[8];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_NEAR(add.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(add.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(add.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(add.local_point().x, 3.5);
      EXPECT_EQ(add.local_point().y, 3.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[9];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_NEAR(down.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(down.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(down.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(down.local_point().x, 3.5);
      EXPECT_EQ(down.local_point().y, 3.5);
    }

    // UP
    {
      const AccessibilityPointerEvent& up = events[10];
      EXPECT_EQ(up.phase(), Phase::UP);
      EXPECT_NEAR(up.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(up.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(up.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(up.local_point().x, 3.5);
      EXPECT_EQ(up.local_point().y, 3.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[11];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_NEAR(remove.ndc_point().x, .4, kNdcEpsilon);
      EXPECT_NEAR(remove.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view.ViewKoid());
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
  auto [root_view, view, compositor_id] = SetUpSingleView(k5x5x1);

  // Scene is now set up, send in the input.
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2.5,2.5) location of the 5x5 display.
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));
  }
  RunLoopUntilIdle();

  // Verify view's events.
  EXPECT_EQ(view.events().size(), 3u);

  view.events().clear();

  // Now, connect the accessibility listener in the middle of a stream.
  MockAccessibilityPointerEventListener listener(input_system());

  // Sends the rest of the stream.
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2.5, 3.5));
    session->Enqueue(pointer.Remove(2.5, 3.5));
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
  auto [root_view, view, compositor_id] = SetUpSingleView(k5x5x1);

  {
    MockAccessibilityPointerEventListener listener(input_system());

    // Scene is now set up; send in the input.
    {
      scenic::Session* const session = root_view.session();
      PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                      /*pointer id*/ 1, PointerEventType::TOUCH);
      // A touch sequence that starts at the (2.5,2.5) location of the 5x5 display.
      session->Enqueue(pointer.Add(2.5, 2.5));
      session->Enqueue(pointer.Down(2.5, 2.5));
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
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2.5, 3.5));
    session->Enqueue(pointer.Remove(2.5, 3.5));
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
  auto [root_view, view, compositor_id] = SetUpSingleView(k5x5x1);

  MockAccessibilityPointerEventListener listener(input_system());
  listener.SetResponses({{1, fuchsia::ui::input::accessibility::EventHandling::REJECTED}});

  // Scene is now set up; send in the input.
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2.5,2.5) location of the 5x5 display.
    session->Enqueue(pointer.Add(2.5, 2.5));  // Reject happens here.
    session->Enqueue(pointer.Down(2.5, 2.5));
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
      EXPECT_EQ(add.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }
    // TODO(rosswang): What's the second one?
  }

  view.events().clear();
  listener.events().clear();

  // Sends the rest of the stream.
  {
    scenic::Session* const session = root_view.session();
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    session->Enqueue(pointer.Up(2.5, 3.5));
    session->Enqueue(pointer.Remove(2.5, 3.5));
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
  auto [root_view, root_resources] = CreateScene();
  scenic::Session* const session = root_view.session();
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

  SessionWrapper view_a = CreateClient("a11y-view-a", std::move(v_a)),
                 view_b = CreateClient("a11y-view-b", std::move(v_b));

  const uint32_t compositor_id = root_resources.compositor.id();

  // Scene is now set up; send in the input.
  {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2.5,2.5) location of the 5x5 display.
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));
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
      EXPECT_EQ(add.viewref_koid(), view_a.ViewKoid());
      EXPECT_EQ(add.local_point().x, 2.5);
      EXPECT_EQ(add.local_point().y, 2.5);
    }

    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_EQ(down.ndc_point().x, 0);
      EXPECT_EQ(down.ndc_point().y, 0);
      EXPECT_EQ(down.viewref_koid(), view_a.ViewKoid());
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
    // A touch sequence that ends at the (1.5,3.5) location of the 5x5 display.
    session->Enqueue(pointer.Up(1.5, 3.5));
    session->Enqueue(pointer.Remove(1.5, 3.5));
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
      EXPECT_EQ(up.viewref_koid(), view_b.ViewKoid());
      EXPECT_EQ(up.local_point().x, 1.5);
      EXPECT_EQ(up.local_point().y, 3.5);
    }

    // REMOVE
    {
      const AccessibilityPointerEvent& remove = events[1];
      EXPECT_EQ(remove.phase(), Phase::REMOVE);
      EXPECT_NEAR(remove.ndc_point().x, -.4, kNdcEpsilon);
      EXPECT_NEAR(remove.ndc_point().y, .4, kNdcEpsilon);
      EXPECT_EQ(remove.viewref_koid(), view_b.ViewKoid());
      EXPECT_EQ(remove.local_point().x, 1.5);
      EXPECT_EQ(remove.local_point().y, 3.5);
    }
  }
}

// This test checks that semantic visibility works as intended. By setting the views to semantically
// invisble it should appear to accessibility as if they weren't hit, but other clients should still
// observe everything as normal.
TEST_F(AccessibilityPointerEventsTest, SemanticallyInvisible_ShouldNotBeSeenByA11y) {
  MockAccessibilityPointerEventListener listener(input_system());
  // Immediately reject the stream.
  listener.SetResponses({{1, fuchsia::ui::input::accessibility::EventHandling::REJECTED}});

  auto [v_a, vh_a] = scenic::ViewTokenPair::New();
  auto [v_b, vh_b] = scenic::ViewTokenPair::New();

  // Set up a scene with two views.
  // Since we need to manipulate the scene graph, go ahead and do this all at function scope.
  auto [root_view, root_resources] = CreateScene();
  scenic::Session* const session = root_view.session();
  scenic::Scene* const scene = &root_resources.scene;

  scenic::ViewHolder view_holder_a(session, std::move(vh_a), "View Holder A"),
      view_holder_b(session, std::move(vh_b), "View Holder B");

  view_holder_a.SetViewProperties(k5x5x1);
  view_holder_b.SetViewProperties(k5x5x1);

  // Translate each view to control elevation.
  view_holder_a.SetTranslation(0, 0, 1);
  view_holder_b.SetTranslation(0, 0, 2);  // B is lower than A.

  view_holder_a.SetSemanticVisibility(false);  // A is semantically invisible.

  // Attach views to the scene.
  scene->AddChild(view_holder_a);
  scene->AddChild(view_holder_b);

  RequestToPresent(session);

  SessionWrapper view_a = CreateClient("a11y-view-a", std::move(v_a)),
                 view_b = CreateClient("a11y-view-b", std::move(v_b));

  // Scene is now set up; send in the input.
  {
    {  // Turn off parallel dispatch.
      fuchsia::ui::input::SetParallelDispatchCmd parallel_dispatch_cmd;
      parallel_dispatch_cmd.parallel_dispatch = false;

      InputCommand input_cmd;
      input_cmd.set_set_parallel_dispatch(std::move(parallel_dispatch_cmd));
      session->Enqueue(std::move(input_cmd));
    }

    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (2.5,2.5) location of the 5x5 display.
    session->Enqueue(pointer.Add(2.5, 2.5));
    session->Enqueue(pointer.Down(2.5, 2.5));
  }
  RunLoopUntilIdle();

  // Should look to A11y like B was the top hit.
  EXPECT_FALSE(listener.events().empty());
  EXPECT_EQ(listener.events().front().viewref_koid(), utils::ExtractKoid(view_b.view_ref()));

  // Should look to the rest like A was the top hit.
  EXPECT_TRUE(view_b.events().empty());

  ASSERT_EQ(view_a.events().size(), 3u);  // 3 since ADD gets translated to ADD + DOWN.
  EXPECT_EQ(view_a.events()[0].pointer().phase, fuchsia::ui::input::PointerEventPhase::ADD);
  EXPECT_TRUE(view_a.events()[1].is_focus());
  EXPECT_EQ(view_a.events()[2].pointer().phase, fuchsia::ui::input::PointerEventPhase::DOWN);
}

// Create a larger 7x7 display, so that the scene (5x5) does not fully cover the display.
class LargeDisplayAccessibilityPointerEventsTest : public AccessibilityPointerEventsTest {
 protected:
  uint32_t test_display_width_px() const override { return 7; }
  uint32_t test_display_height_px() const override { return 7; }
};

// This test has a DOWN event see an empty hit test, which means there is no client that latches.
// However, (1) accessibility should receive initial events, and (2) rejection by accessibility
// (on first MOVE) should trigger the expected focus change.
TEST_F(LargeDisplayAccessibilityPointerEventsTest, NoDownLatchAndA11yRejects) {
  MockAccessibilityPointerEventListener listener(input_system());
  // Respond after three events: ADD / DOWN / MOVE.
  listener.SetResponses({{3, fuchsia::ui::input::accessibility::EventHandling::REJECTED}});

  auto [vt, vht] = scenic::ViewTokenPair::New();

  // Set up a scene with one view.
  auto [root_view, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_view.session();
    scenic::Scene* const scene = &root_resources.scene;
    // Set scene origin (0, 0) to coincide with display (1, 1) -- this translation ensures that a
    // 5x5 view is centered on the display.
    scene->SetTranslation(1, 1, 0);

    scenic::ViewHolder view_holder(session, std::move(vht), "view holder");
    view_holder.SetViewProperties(k5x5x1);
    scene->AddChild(view_holder);

    RequestToPresent(session);
  }

  SessionWrapper view = CreateClient("a11y-single-view", std::move(vt));

  // Transfer focus to view.
  {
    zx_koid_t scene_koid = engine()->scene_graph()->view_tree().focus_chain()[0];
    auto status = engine()->scene_graph()->RequestFocusChange(scene_koid, view.ViewKoid());
    ASSERT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  }
  RunLoopUntilIdle();  // Flush out focus events to clients.

  // Clear out events.
  root_view.events().clear();
  view.events().clear();

  // Setup is finished.  Scene is now set up; send in the input.
  {
    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (0.5,0.5) location of the 7x7 display.
    root_view.session()->Enqueue(pointer.Add(0.5, 0.5));
    root_view.session()->Enqueue(pointer.Down(0.5, 0.5));
  }
  RunLoopUntilIdle();

  // Verify view did not receive events.
  EXPECT_EQ(view.events().size(), 0u);
  // Verify root session did not receive events.
  EXPECT_EQ(root_view.events().size(), 0u);

  // Send in third touch event, which causes accessibility to reject.
  {
    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (0.5,0.5) location of the 7x7 display.
    root_view.session()->Enqueue(pointer.Move(0.5, 0.5));
  }
  RunLoopUntilIdle();

  // Verify view received only the unfocus event.
  {
    const std::vector<InputEvent>& events = view.events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_FALSE(events[0].focus().focused);
  }

  // Verify root session received only the focus event (since we revert to root of focus chain).
  {
    const std::vector<InputEvent>& events = root_view.events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].is_focus());
    EXPECT_TRUE(events[0].focus().focused);
  }
}

// This test has a DOWN event see an empty hit test, which means there is no client that latches.
// However, (1) accessibility should receive initial events, and (2) acceptance by accessibility (on
// first MOVE) means accessibility continues to observe events, despite absence of latch.
TEST_F(LargeDisplayAccessibilityPointerEventsTest, NoDownLatchAndA11yAccepts) {
  MockAccessibilityPointerEventListener listener(input_system());
  // Respond after three events: ADD / DOWN / MOVE.
  listener.SetResponses({{3, fuchsia::ui::input::accessibility::EventHandling::CONSUMED}});

  auto [vt, vht] = scenic::ViewTokenPair::New();

  // Set up a scene with one view.
  auto [root_view, root_resources] = CreateScene();
  {
    scenic::Session* const session = root_view.session();
    scenic::Scene* const scene = &root_resources.scene;
    // Set scene origin (0, 0) to coincide with display (1, 1) -- this translation ensures that a
    // 5x5 view is centered on the display.
    scene->SetTranslation(1, 1, 0);

    scenic::ViewHolder view_holder(session, std::move(vht), "view holder");
    view_holder.SetViewProperties(k5x5x1);
    scene->AddChild(view_holder);

    RequestToPresent(session);
  }

  SessionWrapper view = CreateClient("a11y-single-view", std::move(vt));

  // Transfer focus to view.
  {
    zx_koid_t scene_koid = engine()->scene_graph()->view_tree().focus_chain()[0];
    auto status = engine()->scene_graph()->RequestFocusChange(scene_koid, view.ViewKoid());
    ASSERT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  }
  RunLoopUntilIdle();  // Flush out focus events to clients.

  // Clear out events.
  root_view.events().clear();
  view.events().clear();

  // Setup is finished.  Scene is now set up; send in the input.
  {
    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // A touch sequence that starts at the (0.5,0.5) location of the 7x7 display.
    root_view.session()->Enqueue(pointer.Add(0.5, 0.5));
    root_view.session()->Enqueue(pointer.Down(0.5, 0.5));
  }
  RunLoopUntilIdle();

  // Verify view did not receive events.
  EXPECT_EQ(view.events().size(), 0u);
  // Verify root session did not receive events.
  EXPECT_EQ(root_view.events().size(), 0u);

  // Send in third touch event, which causes accessibility to consume existing and future events.
  {
    PointerCommandGenerator pointer(root_resources.compositor.id(), /*device id*/ 1,
                                    /*pointer id*/ 1, PointerEventType::TOUCH);
    // Send MOVE events *over* the view.
    root_view.session()->Enqueue(pointer.Move(1.5, 1.5));
    root_view.session()->Enqueue(pointer.Move(2.5, 2.5));
  }
  RunLoopUntilIdle();

  // Verify view did not receive events.
  EXPECT_EQ(view.events().size(), 0u);
  // Verify root session did not receive events.
  EXPECT_EQ(root_view.events().size(), 0u);

  // Verify accessibility received 4 events so far.
  {
    const std::vector<AccessibilityPointerEvent>& events = listener.events();
    ASSERT_EQ(events.size(), 4u);

    // ADD
    {
      const AccessibilityPointerEvent& add = events[0];
      EXPECT_EQ(add.phase(), Phase::ADD);
      EXPECT_EQ(add.viewref_koid(), ZX_KOID_INVALID);
    }
    // DOWN
    {
      const AccessibilityPointerEvent& down = events[1];
      EXPECT_EQ(down.phase(), Phase::DOWN);
      EXPECT_EQ(down.viewref_koid(), ZX_KOID_INVALID);
    }
    // MOVE
    {
      const AccessibilityPointerEvent& move = events[2];
      EXPECT_EQ(move.phase(), Phase::MOVE);
      EXPECT_EQ(move.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(move.local_point().x, 0.5);
      EXPECT_EQ(move.local_point().y, 0.5);
    }
    // MOVE
    {
      const AccessibilityPointerEvent& move = events[3];
      EXPECT_EQ(move.phase(), Phase::MOVE);
      EXPECT_EQ(move.viewref_koid(), view.ViewKoid());
      EXPECT_EQ(move.local_point().x, 1.5);
      EXPECT_EQ(move.local_point().y, 1.5);
    }
  }
}

// Injection in TOP_HIT_AND_ANCESTORS_IN_TARGET mode should be delivered to a11y only if the context
// is the root view. This test registers an injector where the context is the root view.
TEST_F(AccessibilityPointerEventsTest, TopHitInjectionByRootView_IsDeliveredToA11y) {
  auto [root_view, child_view, child_view2, compositor_id] = SetUpTwoViews(k5x5x1);
  MockAccessibilityPointerEventListener listener(input_system());

  // Scene is now set up; send in the input.
  {
    RegisterInjector(/*context=*/root_view.view_ref(), /*target=*/child_view2.view_ref(),
                     fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET,
                     fuchsia::ui::pointerinjector::DeviceType::TOUCH,
                     /*extents*/ {{/*min*/ {-100.f, -50.f}, /*max*/ {50.f, 100.f}}});
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::ADD);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  // Verify views have no events.
  EXPECT_TRUE(child_view.events().empty());
  EXPECT_TRUE(child_view2.events().empty());

  // Verify accessibility's events.
  // 5 because ADD gets converted to ADD + DOWN and REMOVE to UP + REMOVE.
  ASSERT_EQ(listener.events().size(), 5u);
  EXPECT_EQ(listener.events()[0].phase(), fuchsia::ui::input::PointerEventPhase::ADD);
  EXPECT_EQ(listener.events()[1].phase(), fuchsia::ui::input::PointerEventPhase::DOWN);
  EXPECT_EQ(listener.events()[2].phase(), fuchsia::ui::input::PointerEventPhase::MOVE);
  EXPECT_EQ(listener.events()[3].phase(), fuchsia::ui::input::PointerEventPhase::UP);
  EXPECT_EQ(listener.events()[4].phase(), fuchsia::ui::input::PointerEventPhase::REMOVE);
}

// Injection in TOP_HIT_AND_ANCESTORS_IN_TARGET mode should be delivered to a11y only if the context
// is the root view. This test registers an injector where the context is further down the graph.
TEST_F(AccessibilityPointerEventsTest, TopHitInjectionByNonRootView_IsNotDeliveredToA11y) {
  auto [root_view, child_view, child_view2, compositor_id] = SetUpTwoViews(k5x5x1);
  MockAccessibilityPointerEventListener listener(input_system());

  // Scene is now set up; send in the input.
  {
    RegisterInjector(/*context=*/child_view.view_ref(), /*target=*/child_view2.view_ref(),
                     fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET,
                     fuchsia::ui::pointerinjector::DeviceType::TOUCH,
                     /*extents*/ {{/*min*/ {-100.f, -50.f}, /*max*/ {50.f, 100.f}}});
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::ADD);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  // Verify child_view2 received the events.
  EXPECT_FALSE(child_view2.events().empty());

  // Verify other view and accessibility have no events.
  EXPECT_TRUE(child_view.events().empty());
  EXPECT_TRUE(listener.events().empty());
}

// Injection in EXCLUSIVE_TARGET mode should never be delivered to a11y. This test sets up
// a valid environment for a11y to get events, except for the DispatchPolicy.
TEST_F(AccessibilityPointerEventsTest, ExclusiveInjectionByRootView_IsNotDeliveredToA11y) {
  auto [root_view, child_view, child_view2, compositor_id] = SetUpTwoViews(k5x5x1);
  MockAccessibilityPointerEventListener listener(input_system());

  // Scene is now set up; send in the input.
  {
    RegisterInjector(/*context=*/root_view.view_ref(), /*target=*/child_view2.view_ref(),
                     fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET,
                     fuchsia::ui::pointerinjector::DeviceType::TOUCH,
                     /*extents*/ {{/*min*/ {-100.f, -50.f}, /*max*/ {50.f, 100.f}}});
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::ADD);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::CHANGE);
    Inject(2.5f, 2.5f, fuchsia::ui::pointerinjector::EventPhase::REMOVE);
    RunLoopUntilIdle();
  }

  // Verify child_view2 received the events.
  EXPECT_FALSE(child_view2.events().empty());

  // Verify other view and accessibility have no events.
  EXPECT_TRUE(child_view.events().empty());
  EXPECT_TRUE(listener.events().empty());
}

}  // namespace
}  // namespace lib_ui_input_tests
