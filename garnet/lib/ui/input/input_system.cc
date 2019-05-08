// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/input_system.h"

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/util/type_utils.h"
#include <lib/fidl/cpp/clone.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/geometry/cpp/formatting.h>
#include <lib/ui/input/cpp/formatting.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/time/time_point.h>
#include <trace/event.h>

#include <memory>

#include "garnet/lib/ui/gfx/engine/hit.h"
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"
#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/session.h"

namespace scenic_impl {
namespace input {

const char* InputSystem::kName = "InputSystem";

using InputCommand = fuchsia::ui::input::Command;
using Phase = fuchsia::ui::input::PointerEventPhase;
using ScenicCommand = fuchsia::ui::scenic::Command;
using fuchsia::ui::input::FocusEvent;
using fuchsia::ui::input::ImeService;
using fuchsia::ui::input::ImeServicePtr;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::KeyboardEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::input::SendKeyboardInputCmd;
using fuchsia::ui::input::SendPointerInputCmd;
using fuchsia::ui::input::SetHardKeyboardDeliveryCmd;
using fuchsia::ui::input::SetParallelDispatchCmd;

namespace {
// Helper for Dispatch[Touch|Mouse]Command.
int64_t NowInNs() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

// TODO(SCN-1278): Remove this.
// Turn two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

// Helper for Dispatch[Touch|Mouse]Command and PerformGlobalHitTest.
escher::ray4 CreateScreenPerpendicularRay(float x, float y) {
  // We set the elevation for the origin point, and Z value for the direction,
  // such that we start above the scene and point into the scene.
  //
  // Scenic flips around Vulkan's camera to the more intuitive "look
  // forward" orientation. The ray must now be stated in terms of the camera's
  // model space, so "taking a step back" translates to "negative Z origin".
  // Similarly, "look at the scene" translates to "positive Z direction".
  //
  // For hit testing, these values work in conjunction with
  // Camera::ProjectRayIntoScene to create an appropriate ray4 that works
  // correctly with the hit tester.
  //
  // During dispatch, we translate an arbitrary pointer's (x,y) device-space
  // coordinates to a View's (x', y') model-space coordinates.
  return {{x, y, 1.f, 1.f},  // Origin as homogeneous point.
          {0.f, 0.f, -1.f, 0.f}};
}

// Helper for Dispatch[Touch|Mouse]Command.
escher::vec2 TransformPointerEvent(const escher::ray4& ray,
                                   const escher::mat4& transform) {
  escher::ray4 local_ray = glm::inverse(transform) * ray;

  // We treat distance as 0 to simplify; otherwise the formula is:
  // hit = homogenize(local_ray.origin + distance * local_ray.direction);
  escher::vec2 hit(escher::homogenize(local_ray.origin));

  FXL_VLOG(2) << "Coordinate transform (device->view): (" << ray.origin.x
              << ", " << ray.origin.x << ")->(" << hit.x << ", " << hit.y
              << ")";
  return hit;
}

// Helper for Dispatch[Touch|Mouse]Command.
glm::mat4 FindGlobalTransform(gfx::ViewPtr view) {
  if (!view || !view->GetViewNode()) {
    return glm::mat4(1.f);  // Default is identity transform.
  }
  return view->GetViewNode()->GetGlobalTransform();
}

// The x and y values are in device (screen) coordinates.
// The initial dispatch logic guarantees a valid compositor and layer stack.
// NOTE: The returned gfx::Hit struct contains a raw Node*, so callers:
//   - must not retain it, or extends its lifetime via Refptr,
//   - must not write into it,
//   - may call const functions against it.
//
// Only the root presenter creates compositors and sends input commands.
// This invariant means this dispatcher context's session, handling an input
// command, also originally created the compositor.
//
std::vector<gfx::Hit> PerformGlobalHitTest(gfx::GfxSystem* gfx_system,
                                           GlobalId compositor_id, float x,
                                           float y) {
  FXL_DCHECK(gfx_system);

  escher::ray4 ray = CreateScreenPerpendicularRay(x, y);
  FXL_VLOG(1) << "HitTest: device point (" << ray.origin.x << ", "
              << ray.origin.y << ")";

  gfx::CompositorWeakPtr compositor = gfx_system->GetCompositor(compositor_id);
  FXL_DCHECK(compositor) << "No compositor, violated invariant.";

  gfx::LayerStackPtr layer_stack = compositor->layer_stack();
  FXL_DCHECK(layer_stack.get()) << "No layer stack, violated invariant.";

  auto hit_tester = std::make_unique<gfx::GlobalHitTester>();
  std::vector<gfx::Hit> hits = layer_stack->HitTest(ray, hit_tester.get());

  FXL_VLOG(1) << "Hits acquired, count: " << hits.size();

  if (FXL_VLOG_IS_ON(2)) {
    for (size_t i = 0; i < hits.size(); ++i) {
      FXL_VLOG(2) << "\tHit[" << i << "]: " << hits[i].node->global_id();
    }
  }

  return hits;
}

// Helper for DispatchCommand.
PointerEvent ClonePointerWithCoords(const PointerEvent& event, float x,
                                    float y) {
  PointerEvent clone;
  fidl::Clone(event, &clone);
  clone.x = x;
  clone.y = y;
  return clone;
}

// Helper for DispatchTouchCommand.
// Ensure sessions get each event just once: stamp out duplicate
// sessions in the rest of the hits. This assumes:
// - each session has at most one View
// - each session receives at most one hit per View
// TODO(SCN-935): Return full set of hits to each client.
void RemoveHitsFromSameSession(SessionId session_id, size_t start_idx,
                               std::vector<gfx::ViewPtr>* views) {
  FXL_DCHECK(views);
  for (size_t k = start_idx; k < views->size(); ++k) {
    if ((*views)[k] && ((*views)[k]->session()->id() == session_id)) {
      (*views)[k] = nullptr;
    }
  }
}

// Helper for Dispatch[Touch|Mouse]Command.
bool IsFocusChange(gfx::ViewPtr view) {
  FXL_DCHECK(view);

  if (view->connected()) {
    return view->view_holder()->GetViewProperties().focus_change;
  }

  return true;  // Implicitly, all Views can receive focus.
}
}  // namespace

InputSystem::InputSystem(SystemContext context, gfx::GfxSystem* gfx_system)
    : System(std::move(context), /*initialized_after_construction*/ false),
      gfx_system_(gfx_system) {
  FXL_CHECK(gfx_system_);
  gfx_system_->AddInitClosure([this]() {
    SetToInitialized();

    text_sync_service_ =
        this->context()->app_context()->svc()->Connect<ImeService>();
    text_sync_service_.set_error_handler([](zx_status_t status) {
      FXL_LOG(ERROR) << "Scenic lost connection to TextSync";
    });

    FXL_LOG(INFO) << "Scenic input system initialized.";
  });
}

CommandDispatcherUniquePtr InputSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return CommandDispatcherUniquePtr(
      new InputCommandDispatcher(std::move(context), gfx_system_, this),
      // Custom deleter.
      [](CommandDispatcher* cd) { delete cd; });
}

InputCommandDispatcher::InputCommandDispatcher(
    CommandDispatcherContext command_dispatcher_context,
    gfx::GfxSystem* gfx_system, InputSystem* input_system)
    : CommandDispatcher(std::move(command_dispatcher_context)),
      gfx_system_(gfx_system),
      input_system_(input_system) {
  FXL_CHECK(gfx_system_);
  FXL_CHECK(input_system_);
}

void InputCommandDispatcher::DispatchCommand(ScenicCommand command) {
  TRACE_DURATION("input", "dispatch_command", "command", "ScenicCmd");
  FXL_DCHECK(command.Which() == ScenicCommand::Tag::kInput);

  InputCommand& input = command.input();
  if (input.is_send_keyboard_input()) {
    DispatchCommand(std::move(input.send_keyboard_input()));
  } else if (input.is_send_pointer_input()) {
    // Compositor and layer stack required for dispatch.
    GlobalId compositor_id(command_dispatcher_context()->session_id(),
                           input.send_pointer_input().compositor_id);
    gfx::CompositorWeakPtr compositor =
        gfx_system_->GetCompositor(compositor_id);
    if (!compositor)
      return;  // It's legal to race against GFX's compositor setup.

    gfx::LayerStackPtr layer_stack = compositor->layer_stack();
    if (!layer_stack)
      return;  // It's legal to race against GFX's layer stack setup.

    DispatchCommand(std::move(input.send_pointer_input()));
  } else if (input.is_set_hard_keyboard_delivery()) {
    DispatchCommand(std::move(input.set_hard_keyboard_delivery()));
  } else if (input.is_set_parallel_dispatch()) {
    DispatchCommand(std::move(input.set_parallel_dispatch()));
  }
}

void InputCommandDispatcher::DispatchCommand(
    const SendPointerInputCmd command) {
  TRACE_DURATION("input", "dispatch_command", "command", "PointerCmd");
  const PointerEventType& event_type = command.pointer_event.type;
  if (event_type == PointerEventType::TOUCH) {
    DispatchTouchCommand(std::move(command));
  } else if (event_type == PointerEventType::MOUSE) {
    DispatchMouseCommand(std::move(command));
  } else {
    // TODO(SCN-940), TODO(SCN-164): Stylus support needs to account for HOVER
    // events, which need to trigger an additional hit test on the DOWN event
    // and send CANCEL events to disassociated clients.
    FXL_LOG(INFO) << "Add stylus support.";
  }
}

// The touch state machine comprises ADD/DOWN/MOVE*/UP/REMOVE. Some notes:
//  - We assume one touchscreen device, and use the device-assigned finger ID.
//  - Touch ADD associates the following ADD/DOWN/MOVE*/UP/REMOVE event sequence
//    with the set of clients available at that time. To enable gesture
//    disambiguation, we perform parallel dispatch to all clients.
//  - Touch DOWN triggers a focus change, but honors the no-focus property.
//  - Touch REMOVE drops the association between event stream and client.
void InputCommandDispatcher::DispatchTouchCommand(
    const SendPointerInputCmd command) {
  TRACE_DURATION("input", "dispatch_command", "command", "TouchCmd");
  trace_flow_id_t trace_id = PointerTraceHACK(
      command.pointer_event.radius_major, command.pointer_event.radius_minor);
  TRACE_FLOW_END("input", "dispatch_event_to_scenic", trace_id);

  const uint32_t pointer_id = command.pointer_event.pointer_id;
  const Phase pointer_phase = command.pointer_event.phase;
  const float pointer_x = command.pointer_event.x;
  const float pointer_y = command.pointer_event.y;

  FXL_DCHECK(command.pointer_event.type == PointerEventType::TOUCH);
  FXL_DCHECK(pointer_phase != Phase::HOVER)
      << "Oops, touch device had unexpected HOVER event.";

  if (pointer_phase == Phase::ADD) {
    GlobalId compositor_id(command_dispatcher_context()->session_id(),
                           command.compositor_id);
    const std::vector<gfx::Hit> hits =
        PerformGlobalHitTest(gfx_system_, compositor_id, pointer_x, pointer_y);

    // Find input targets.  Honor the "input masking" view property.
    ViewStack hit_views;
    {
      // Find the View for each hit. Don't hold on to these RefPtrs!
      std::vector<gfx::ViewPtr> views;
      views.reserve(hits.size());
      for (const gfx::Hit& hit : hits) {
        FXL_DCHECK(hit.node);  // Raw ptr, use it and let go.
        views.push_back(hit.node->FindOwningView());
      }
      FXL_DCHECK(hits.size() == views.size());

      // Find the global transform for each hit, fill out hit_views.
      for (size_t i = 0; i < hits.size(); ++i) {
        if (gfx::ViewPtr view = views[i]) {
          hit_views.stack.push_back(
              {view->global_id(), FindGlobalTransform(view)});
          if (/*TODO(SCN-919): view_id may mask input */ false) {
            break;
          }
          // Don't do this. Refer to comment on RemoveHitsFromSameSession.
          RemoveHitsFromSameSession(view->session()->id(), i + 1, &views);
        }
      }

      // Determine focusability of top-level view.
      if (views.size() > 0 && views[0]) {
        hit_views.focus_change = IsFocusChange(views[0]);
      }
    }
    FXL_VLOG(1) << "View stack of hits: " << hit_views;

    // Save targets for consistent delivery of touch events.
    touch_targets_[pointer_id] = hit_views;

  } else if (pointer_phase == Phase::DOWN) {
    // New focus can be: (1) empty (if no views), or (2) the old focus (either
    // deliberately, or by the no-focus property), or (3) another view.
    GlobalId new_focus;
    if (!touch_targets_[pointer_id].stack.empty()) {
      if (touch_targets_[pointer_id].focus_change) {
        new_focus = touch_targets_[pointer_id].stack[0].view_id;
      } else {
        new_focus = focus_;  // No focus change.
      }
    }
    FXL_VLOG(1) << "Focus, old and new: " << focus_ << " vs " << new_focus;

    // Deliver focus events.
    if (focus_ != new_focus) {
      const int64_t focus_time = NowInNs();
      if (focus_) {
        FocusEvent event;
        event.event_time = focus_time;
        event.focused = false;
        EnqueueEventToView(focus_, event);
        FXL_VLOG(1) << "Input focus lost by " << focus_;
      }
      if (new_focus) {
        FocusEvent event;
        event.event_time = focus_time;
        event.focused = true;
        EnqueueEventToView(new_focus, event);
        FXL_VLOG(1) << "Input focus gained by " << new_focus;
      }
      focus_ = new_focus;
    }
  }

  // Input delivery must be parallel; needed for gesture disambiguation.
  for (const auto& entry : touch_targets_[pointer_id].stack) {
    escher::ray4 screen_ray =
        CreateScreenPerpendicularRay(pointer_x, pointer_y);
    escher::vec2 hit =
        TransformPointerEvent(screen_ray, entry.global_transform);

    auto clone = ClonePointerWithCoords(command.pointer_event, hit.x, hit.y);
    EnqueueEventToView(entry.view_id, std::move(clone));

    if (!parallel_dispatch_) {
      break;  // TODO(SCN-1047): Remove when gesture disambiguation is ready.
    }
  }

  if (pointer_phase == Phase::REMOVE || pointer_phase == Phase::CANCEL) {
    touch_targets_.erase(pointer_id);
  }
}

// The mouse state machine is simpler, comprising MOVE*-DOWN/MOVE*/UP-MOVE*. Its
// behavior is similar to touch events, but with some differences.
//  - There can be multiple mouse devices, so we track each device individually.
//  - Mouse DOWN associates the following DOWN/MOVE*/UP event sequence with one
//    particular client: the top-hit View. Mouse events aren't associated with
//    gestures, so there is no parallel dispatch.
//  - Mouse DOWN triggers a focus change, but honors the no-focus property.
//  - Mouse UP drops the association between event stream and client.
//  - For an unassociated MOVE event, we perform a hit test, and send the
//    top-most client this MOVE event. Focus does not change for unassociated
//    MOVEs.
//  - The hit test must account for the mouse cursor itself, which today is
//    owned by the root presenter. The nodes associated with visible mouse
//    cursors(!) do not roll up to any View (as expected), but may appear in the
//    hit test; our dispatch needs to account for such behavior.
// TODO(SCN-1078): Enhance trackpad support.
void InputCommandDispatcher::DispatchMouseCommand(
    const SendPointerInputCmd command) {
  const uint32_t device_id = command.pointer_event.device_id;
  const Phase pointer_phase = command.pointer_event.phase;
  const float pointer_x = command.pointer_event.x;
  const float pointer_y = command.pointer_event.y;

  FXL_DCHECK(command.pointer_event.type == PointerEventType::MOUSE);
  FXL_DCHECK(pointer_phase != Phase::ADD && pointer_phase != Phase::REMOVE &&
             pointer_phase != Phase::HOVER)
      << "Oops, mouse device (id=" << device_id
      << ") had an unexpected event: " << pointer_phase;

  if (pointer_phase == Phase::DOWN) {
    GlobalId compositor_id(command_dispatcher_context()->session_id(),
                           command.compositor_id);
    const std::vector<gfx::Hit> hits =
        PerformGlobalHitTest(gfx_system_, compositor_id, pointer_x, pointer_y);

    // Find top-hit target and associated properties.
    // NOTE: We may hit various mouse cursors (owned by root presenter), so keep
    // going until we find a hit with a valid owning View.
    ViewStack hit_view;
    for (gfx::Hit hit : hits) {
      FXL_DCHECK(hit.node);  // Raw ptr, use it and let go.
      if (gfx::ViewPtr view = hit.node->FindOwningView()) {
        hit_view.stack.push_back(
            {view->global_id(), FindGlobalTransform(view)});
        hit_view.focus_change = IsFocusChange(view);
        break;  // Just need the first one.
      }
    }
    FXL_VLOG(1) << "View hit: " << hit_view;

    // New focus can be: (1) empty (if no views), or (2) the old focus (either
    // deliberately, or by the no-focus property), or (3) another view.
    GlobalId new_focus;
    if (!hit_view.stack.empty()) {
      if (hit_view.focus_change) {
        new_focus = hit_view.stack[0].view_id;
      } else {
        new_focus = focus_;  // No focus change.
      }
    }
    FXL_VLOG(1) << "Focus, old and new: " << focus_ << " vs " << new_focus;

    // Deliver focus events.
    if (focus_ != new_focus) {
      const int64_t focus_time = NowInNs();
      if (focus_) {
        FocusEvent event;
        event.event_time = focus_time;
        event.focused = false;
        EnqueueEventToView(focus_, event);
        FXL_VLOG(1) << "Input focus lost by " << focus_;
      }
      if (new_focus) {
        FocusEvent event;
        event.event_time = focus_time;
        event.focused = true;
        EnqueueEventToView(new_focus, event);
        FXL_VLOG(1) << "Input focus gained by " << new_focus;
      }
      focus_ = new_focus;
    }

    // Save target for consistent delivery of mouse events.
    mouse_targets_[device_id] = hit_view;
  }

  if (mouse_targets_.count(device_id) > 0 &&  // Tracking this device, and
      mouse_targets_[device_id].stack.size() > 0) {  // target view exists.
    const auto& entry = mouse_targets_[device_id].stack[0];

    escher::ray4 screen_ray =
        CreateScreenPerpendicularRay(pointer_x, pointer_y);
    escher::vec2 hit =
        TransformPointerEvent(screen_ray, entry.global_transform);

    auto clone = ClonePointerWithCoords(command.pointer_event, hit.x, hit.y);
    EnqueueEventToView(entry.view_id, std::move(clone));
  }

  if (pointer_phase == Phase::UP || pointer_phase == Phase::CANCEL) {
    mouse_targets_.erase(device_id);
  }

  // Deal with unassociated MOVE events.
  if (pointer_phase == Phase::MOVE && mouse_targets_.count(device_id) == 0) {
    GlobalId compositor_id(command_dispatcher_context()->session_id(),
                           command.compositor_id);
    const std::vector<gfx::Hit> hits =
        PerformGlobalHitTest(gfx_system_, compositor_id, pointer_x, pointer_y);
    // Find top-hit target and send it this move event.
    // NOTE: We may hit various mouse cursors (owned by root presenter), so keep
    // going until we find a hit with a valid owning View.
    GlobalId view_id;
    for (gfx::Hit hit : hits) {
      FXL_DCHECK(hit.node);  // Raw ptr, use it and let go.
      if (gfx::ViewPtr view = hit.node->FindOwningView()) {
        view_id = view->global_id();

        escher::ray4 screen_ray =
            CreateScreenPerpendicularRay(pointer_x, pointer_y);
        glm::mat4 global_transform = FindGlobalTransform(view);
        escher::vec2 hit = TransformPointerEvent(screen_ray, global_transform);

        auto clone =
            ClonePointerWithCoords(command.pointer_event, hit.x, hit.y);
        EnqueueEventToView(view_id, std::move(clone));
        break;  // Just need the first one.
      }
    }

    FXL_VLOG(2) << "View hit: " << view_id;
  }
}

void InputCommandDispatcher::DispatchCommand(
    const SendKeyboardInputCmd command) {
  // Send keyboard events to the active focus via Text Sync.
  EnqueueEventToTextSync(focus_, command.keyboard_event);

  // Clients may request direct delivery.
  if (focus_.session_id > 0 &&
      input_system_->hard_keyboard_requested().count(focus_.session_id) > 0) {
    EnqueueEventToView(focus_, command.keyboard_event);
  }
}

void InputCommandDispatcher::DispatchCommand(
    const SetHardKeyboardDeliveryCmd command) {
  const SessionId session_id = command_dispatcher_context()->session_id();
  FXL_VLOG(2) << "Hard keyboard events, session_id=" << session_id
              << ", delivery_request="
              << (command.delivery_request ? "on" : "off");

  if (command.delivery_request) {
    // Take this opportunity to remove dead sessions.
    for (SessionId id : input_system_->hard_keyboard_requested()) {
      if (gfx_system_->GetSession(id) == nullptr) {
        input_system_->hard_keyboard_requested().erase(id);
      }
    }

    input_system_->hard_keyboard_requested().insert(session_id);
  } else {
    input_system_->hard_keyboard_requested().erase(session_id);
  }
}

void InputCommandDispatcher::DispatchCommand(
    const SetParallelDispatchCmd command) {
  FXL_LOG(INFO) << "Scenic: Parallel dispatch is turned "
                << (command.parallel_dispatch ? "ON" : "OFF");
  parallel_dispatch_ = command.parallel_dispatch;
}

void InputCommandDispatcher::EnqueueEventToView(GlobalId view_id,
                                                FocusEvent focus) {
  if (gfx::Session* session = gfx_system_->GetSession(view_id.session_id)) {
    InputEvent event;
    event.set_focus(std::move(focus));

    session->EnqueueEvent(std::move(event));
  }
}

void InputCommandDispatcher::EnqueueEventToView(GlobalId view_id,
                                                PointerEvent pointer) {
  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "pointer");
  if (gfx::Session* session = gfx_system_->GetSession(view_id.session_id)) {
    trace_flow_id_t trace_id =
        PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
    TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", trace_id);

    InputEvent event;
    event.set_pointer(std::move(pointer));

    session->EnqueueEvent(std::move(event));
  }
}

void InputCommandDispatcher::EnqueueEventToView(GlobalId view_id,
                                                KeyboardEvent keyboard) {
  if (gfx::Session* session = gfx_system_->GetSession(view_id.session_id)) {
    InputEvent event;
    event.set_keyboard(std::move(keyboard));

    session->EnqueueEvent(std::move(event));
  }
}

void InputCommandDispatcher::EnqueueEventToTextSync(GlobalId view_id,
                                                    KeyboardEvent keyboard) {
  ImeServicePtr& text_sync = input_system_->text_sync_service();
  if (text_sync && text_sync.is_bound()) {
    InputEvent event;
    event.set_keyboard(std::move(keyboard));

    text_sync->InjectInput(std::move(event));
  }
}

}  // namespace input
}  // namespace scenic_impl
