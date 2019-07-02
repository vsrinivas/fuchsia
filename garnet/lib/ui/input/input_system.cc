// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/input_system.h"

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/geometry/cpp/formatting.h>
#include <lib/ui/input/cpp/formatting.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/time/time_point.h>
#include <trace/event.h>

#include <algorithm>
#include <memory>
#include <vector>

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
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/util/type_utils.h"

namespace scenic_impl {
namespace input {

const char* InputSystem::kName = "InputSystem";

using InputCommand = fuchsia::ui::input::Command;
using Phase = fuchsia::ui::input::PointerEventPhase;
using ScenicCommand = fuchsia::ui::scenic::Command;
using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
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
int64_t NowInNs() { return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds(); }

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
  // coordinates to a View's (x', y') model-space coordinates. We clamp the
  // (x,y) coordinates to its lower-bound int value, and then jitter the
  // coordinates by (0.5, 0.5) to make sure the ray always starts off in the
  // center of the pixel.
  return {{std::floor(x) + 0.5f, std::floor(y) + 0.5f, 1.f, 1.f},  // Origin as homogeneous point.
          {0.f, 0.f, -1.f, 0.f}};
}

// Helper for Dispatch[Touch|Mouse]Command.
escher::vec2 TransformPointerEvent(const escher::ray4& ray, const escher::mat4& transform) {
  escher::ray4 local_ray = glm::inverse(transform) * ray;

  // We treat distance as 0 to simplify; otherwise the formula is:
  // hit = homogenize(local_ray.origin + distance * local_ray.direction);
  escher::vec2 hit(escher::homogenize(local_ray.origin));

  FXL_VLOG(2) << "Coordinate transform (device->view): (" << ray.origin.x << ", " << ray.origin.x
              << ")->(" << hit.x << ", " << hit.y << ")";
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
std::vector<gfx::Hit> PerformGlobalHitTest(gfx::GfxSystem* gfx_system, GlobalId compositor_id,
                                           float x, float y) {
  FXL_DCHECK(gfx_system);

  escher::ray4 ray = CreateScreenPerpendicularRay(x, y);
  FXL_VLOG(1) << "HitTest: device point (" << ray.origin.x << ", " << ray.origin.y << ")";

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
PointerEvent ClonePointerWithCoords(const PointerEvent& event, float x, float y) {
  PointerEvent clone;
  fidl::Clone(event, &clone);
  clone.x = x;
  clone.y = y;
  return clone;
}

// Helper for EnqueueEventToView.
// Builds a pointer event with local view coordinates.
PointerEvent BuildLocalPointerEvent(const PointerEvent& pointer_event,
                                    const ViewStack::Entry& view_info) {
  PointerEvent local;
  const float global_x = pointer_event.x;
  const float global_y = pointer_event.y;
  escher::ray4 screen_ray = CreateScreenPerpendicularRay(global_x, global_y);
  escher::vec2 hit = TransformPointerEvent(screen_ray, view_info.global_transform);
  local = ClonePointerWithCoords(pointer_event, hit.x, hit.y);
  return local;
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

// Helper function to build an AccessibilityPointerEvent when there is a
// registered accessibility listener.
AccessibilityPointerEvent BuildAccessibilityPointerEvent(const PointerEvent& global,
                                                         const PointerEvent& local,
                                                         uint64_t viewref_koid) {
  AccessibilityPointerEvent event;
  event.set_event_time(global.event_time);
  event.set_device_id(global.device_id);
  event.set_pointer_id(global.pointer_id);
  event.set_type(global.type);
  event.set_phase(global.phase);
  event.set_global_point({global.x, global.y});
  event.set_viewref_koid(viewref_koid);
  if (viewref_koid != ZX_KOID_INVALID) {
    event.set_local_point({local.x, local.y});
  }
  return event;
}
}  // namespace

InputSystem::InputSystem(SystemContext context, gfx::GfxSystem* gfx_system)
    : System(std::move(context), /*initialized_after_construction*/ false),
      gfx_system_(gfx_system) {
  FXL_CHECK(gfx_system_);
  gfx_system_->AddInitClosure([this]() {
    SetToInitialized();

    text_sync_service_ = this->context()->app_context()->svc()->Connect<ImeService>();
    text_sync_service_.set_error_handler(
        [](zx_status_t status) { FXL_LOG(ERROR) << "Scenic lost connection to TextSync"; });

    this->context()->app_context()->outgoing()->AddPublicService(
        accessibility_pointer_event_registry_.GetHandler(this));

    FXL_LOG(INFO) << "Scenic input system initialized.";
  });
}

CommandDispatcherUniquePtr InputSystem::CreateCommandDispatcher(CommandDispatcherContext context) {
  return CommandDispatcherUniquePtr(
      new InputCommandDispatcher(std::move(context), gfx_system_, this),
      // Custom deleter.
      [](CommandDispatcher* cd) { delete cd; });
}

void InputSystem::Register(
    fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
        pointer_event_listener,
    RegisterCallback callback) {
  if (!accessibility_pointer_event_listener_) {
    accessibility_pointer_event_listener_.Bind(std::move(pointer_event_listener));
    callback(/*success=*/true);
  } else {
    // An accessibility listener is already registered.
    callback(/*success=*/false);
  }
}

InputCommandDispatcher::InputCommandDispatcher(CommandDispatcherContext command_dispatcher_context,
                                               gfx::GfxSystem* gfx_system,
                                               InputSystem* input_system)
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
    gfx::CompositorWeakPtr compositor = gfx_system_->GetCompositor(compositor_id);
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

void InputCommandDispatcher::DispatchCommand(const SendPointerInputCmd command) {
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
void InputCommandDispatcher::DispatchTouchCommand(const SendPointerInputCmd command) {
  TRACE_DURATION("input", "dispatch_command", "command", "TouchCmd");
  trace_flow_id_t trace_id =
      PointerTraceHACK(command.pointer_event.radius_major, command.pointer_event.radius_minor);
  TRACE_FLOW_END("input", "dispatch_event_to_scenic", trace_id);

  const uint32_t pointer_id = command.pointer_event.pointer_id;
  const Phase pointer_phase = command.pointer_event.phase;
  const float pointer_x = command.pointer_event.x;
  const float pointer_y = command.pointer_event.y;

  const bool a11y_enabled = ShouldForwardAccessibilityPointerEvents();

  FXL_DCHECK(command.pointer_event.type == PointerEventType::TOUCH);
  FXL_DCHECK(pointer_phase != Phase::HOVER) << "Oops, touch device had unexpected HOVER event.";

  if (pointer_phase == Phase::ADD) {
    GlobalId compositor_id(command_dispatcher_context()->session_id(), command.compositor_id);
    const std::vector<gfx::Hit> hits =
        PerformGlobalHitTest(gfx_system_, compositor_id, pointer_x, pointer_y);

    // Find input targets.  Honor the "input masking" view property.
    ViewStack hit_views;
    bool is_focus_change = true;
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
          hit_views.stack.push_back({view->global_id(), FindGlobalTransform(view)});
          if (/*TODO(SCN-919): view_id may mask input */ false) {
            break;
          }
          // Don't do this. Refer to comment on RemoveHitsFromSameSession.
          RemoveHitsFromSameSession(view->session()->id(), i + 1, &views);
        }
      }

      // Determine focusability of top-level view.
      if (views.size() > 0 && views[0]) {
        is_focus_change = IsFocusChange(views[0]);
        hit_views.focus_change = is_focus_change;
      }
    }
    FXL_VLOG(1) << "View stack of hits: " << hit_views;

    // Save targets for consistent delivery of touch events.
    touch_targets_[pointer_id] = hit_views;
    // If there is an accessibility pointer event listener enabled, an ADD event
    // means that a new pointer id stream started.
    if (a11y_enabled) {
      pointer_event_buffer_->AddStream(pointer_id, is_focus_change);
    }
  } else if (pointer_phase == Phase::DOWN) {
    // If accessibility listener is on, focus change events must be sent only if
    // the stream is rejected. This way, this operation is deferred.
    if (!a11y_enabled) {
      // New focus can be: (1) empty (if no views), or (2) the old focus (either
      // deliberately, or by the no-focus property), or (3) another view.
      if (!touch_targets_[pointer_id].stack.empty()) {
        const ViewStack::Entry& view_info = touch_targets_[pointer_id].stack[0];
        MaybeChangeFocus(touch_targets_[pointer_id].focus_change, view_info);
      }
    }
  }
  // Input delivery must be parallel; needed for gesture disambiguation.
  std::vector<std::pair<ViewStack::Entry, PointerEvent>> deferred_events;
  for (const auto& entry : touch_targets_[pointer_id].stack) {
    PointerEvent clone;
    fidl::Clone(command.pointer_event, &clone);
    if (a11y_enabled) {
      deferred_events.emplace_back(entry, std::move(clone));
    } else {
      EnqueueEventToView(entry, std::move(clone));
    }
    if (!parallel_dispatch_) {
      break;  // TODO(SCN-1047): Remove when gesture disambiguation is ready.
    }
  }
  FXL_DCHECK(a11y_enabled || deferred_events.empty())
      << "When a11y pointer forwarding is off, never defer events.";
  if (a11y_enabled && !deferred_events.empty()) {
    // TODO(lucasradaelli): pass the viewref_koid once it exists.
    const auto& local_pointer_event = BuildLocalPointerEvent(
        /*pointer_event=*/deferred_events[0].second,
        /*view_info=*/deferred_events[0].first);  // The top most hit.
    AccessibilityPointerEvent accessibility_pointer_event =
        BuildAccessibilityPointerEvent(command.pointer_event, local_pointer_event,
                                       /*viewref_koid=*/ZX_KOID_INVALID);
    pointer_event_buffer_->AddEvents(pointer_id, std::move(deferred_events),
                                     std::move(accessibility_pointer_event));
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
void InputCommandDispatcher::DispatchMouseCommand(const SendPointerInputCmd command) {
  const uint32_t device_id = command.pointer_event.device_id;
  const Phase pointer_phase = command.pointer_event.phase;
  const float pointer_x = command.pointer_event.x;
  const float pointer_y = command.pointer_event.y;

  FXL_DCHECK(command.pointer_event.type == PointerEventType::MOUSE);
  FXL_DCHECK(pointer_phase != Phase::ADD && pointer_phase != Phase::REMOVE &&
             pointer_phase != Phase::HOVER)
      << "Oops, mouse device (id=" << device_id << ") had an unexpected event: " << pointer_phase;

  if (pointer_phase == Phase::DOWN) {
    GlobalId compositor_id(command_dispatcher_context()->session_id(), command.compositor_id);
    const std::vector<gfx::Hit> hits =
        PerformGlobalHitTest(gfx_system_, compositor_id, pointer_x, pointer_y);

    // Find top-hit target and associated properties.
    // NOTE: We may hit various mouse cursors (owned by root presenter), so keep
    // going until we find a hit with a valid owning View.
    ViewStack hit_view;
    for (gfx::Hit hit : hits) {
      FXL_DCHECK(hit.node);  // Raw ptr, use it and let go.
      if (gfx::ViewPtr view = hit.node->FindOwningView()) {
        hit_view.stack.push_back({view->global_id(), FindGlobalTransform(view)});
        hit_view.focus_change = IsFocusChange(view);
        break;  // Just need the first one.
      }
    }
    FXL_VLOG(1) << "View hit: " << hit_view;

    // New focus can be: (1) empty (if no views), or (2) the old focus (either
    // deliberately, or by the no-focus property), or (3) another view.
    if (!hit_view.stack.empty()) {
      MaybeChangeFocus(hit_view.focus_change, hit_view.stack[0]);
    }

    // Save target for consistent delivery of mouse events.
    mouse_targets_[device_id] = hit_view;
  }

  if (mouse_targets_.count(device_id) > 0 &&         // Tracking this device, and
      mouse_targets_[device_id].stack.size() > 0) {  // target view exists.
    const auto& entry = mouse_targets_[device_id].stack[0];
    PointerEvent clone;
    fidl::Clone(command.pointer_event, &clone);
    EnqueueEventToView(entry, std::move(clone));
  }

  if (pointer_phase == Phase::UP || pointer_phase == Phase::CANCEL) {
    mouse_targets_.erase(device_id);
  }

  // Deal with unassociated MOVE events.
  if (pointer_phase == Phase::MOVE && mouse_targets_.count(device_id) == 0) {
    GlobalId compositor_id(command_dispatcher_context()->session_id(), command.compositor_id);
    const std::vector<gfx::Hit> hits =
        PerformGlobalHitTest(gfx_system_, compositor_id, pointer_x, pointer_y);
    // Find top-hit target and send it this move event.
    // NOTE: We may hit various mouse cursors (owned by root presenter), so keep
    // going until we find a hit with a valid owning View.
    GlobalId view_id;
    for (gfx::Hit hit : hits) {
      FXL_DCHECK(hit.node);  // Raw ptr, use it and let go.
      if (gfx::ViewPtr view = hit.node->FindOwningView()) {
        ViewStack::Entry view_info;
        view_info.view_id = view->global_id();
        view_info.global_transform = FindGlobalTransform(view);
        PointerEvent clone;
        fidl::Clone(command.pointer_event, &clone);
        EnqueueEventToView(view_info, std::move(clone));
        break;  // Just need the first one.
      }
    }

    FXL_VLOG(2) << "View hit: " << view_id;
  }
}

void InputCommandDispatcher::DispatchCommand(const SendKeyboardInputCmd command) {
  // Send keyboard events to the active focus via Text Sync.
  EnqueueEventToTextSync(focus_, command.keyboard_event);

  // Clients may request direct delivery.
  if (focus_.session_id > 0 &&
      input_system_->hard_keyboard_requested().count(focus_.session_id) > 0) {
    EnqueueEventToView(focus_, command.keyboard_event);
  }
}

void InputCommandDispatcher::DispatchCommand(const SetHardKeyboardDeliveryCmd command) {
  const SessionId session_id = command_dispatcher_context()->session_id();
  FXL_VLOG(2) << "Hard keyboard events, session_id=" << session_id
              << ", delivery_request=" << (command.delivery_request ? "on" : "off");

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

void InputCommandDispatcher::DispatchCommand(const SetParallelDispatchCmd command) {
  FXL_LOG(INFO) << "Scenic: Parallel dispatch is turned "
                << (command.parallel_dispatch ? "ON" : "OFF");
  parallel_dispatch_ = command.parallel_dispatch;
}

void InputCommandDispatcher::EnqueueEventToView(GlobalId view_id, FocusEvent focus) {
  if (gfx::Session* session = gfx_system_->GetSession(view_id.session_id)) {
    InputEvent event;
    event.set_focus(std::move(focus));

    session->EnqueueEvent(std::move(event));
  }
}

void InputCommandDispatcher::EnqueueEventToView(const ViewStack::Entry& view_info,
                                                PointerEvent pointer) {
  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "pointer");
  if (gfx::Session* session = gfx_system_->GetSession(view_info.view_id.session_id)) {
    trace_flow_id_t trace_id = PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
    TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", trace_id);

    auto local_pointer_event = BuildLocalPointerEvent(pointer, view_info);
    InputEvent event;
    event.set_pointer(std::move(local_pointer_event));

    session->EnqueueEvent(std::move(event));
  }
}

void InputCommandDispatcher::EnqueueEventToView(GlobalId view_id, KeyboardEvent keyboard) {
  if (gfx::Session* session = gfx_system_->GetSession(view_id.session_id)) {
    InputEvent event;
    event.set_keyboard(std::move(keyboard));

    session->EnqueueEvent(std::move(event));
  }
}

void InputCommandDispatcher::EnqueueEventToTextSync(GlobalId view_id, KeyboardEvent keyboard) {
  ImeServicePtr& text_sync = input_system_->text_sync_service();
  if (text_sync && text_sync.is_bound()) {
    InputEvent event;
    event.set_keyboard(std::move(keyboard));

    text_sync->InjectInput(std::move(event));
  }
}

bool InputCommandDispatcher::ShouldForwardAccessibilityPointerEvents() {
  if (input_system_->IsAccessibilityPointerEventForwardingEnabled()) {
    // If the buffer was not initialized yet, perform the following sanity
    // check: make sure to send active pointer event streams to their final
    // location and do not send them to the a11y listener.
    if (!pointer_event_buffer_) {
      pointer_event_buffer_ = std::make_unique<PointerEventBuffer>(this);
      for (const auto& kv : touch_targets_) {
        // Force a reject in all active pointer IDs. When a new stream arrives,
        // they will automatically be sent for the a11y listener decide
        // what to do with them as the status will change to WAITING_RESPONSE.
        pointer_event_buffer_->SetActiveStreamInfo(
            /*pointer_id=*/kv.first, PointerEventBuffer::PointerIdStreamStatus::REJECTED,
            /*focus_change=*/false);
      }
    }
    return true;
  } else if (pointer_event_buffer_) {
    // The listener disconnected. Release held events, delete the buffer.
    pointer_event_buffer_.reset();
  }
  return false;
}

void InputCommandDispatcher::MaybeChangeFocus(bool focus_change,
                                              const ViewStack::Entry& view_info) {
  GlobalId new_focus;
  if (focus_change) {
    new_focus = view_info.view_id;
  } else {
    new_focus = focus_;  // No focus change.
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

InputCommandDispatcher::PointerEventBuffer::PointerEventBuffer(InputCommandDispatcher* dispatcher)
    : dispatcher_(dispatcher) {
  FXL_DCHECK(dispatcher_) << "Dispatcher can't be NULL.";
}

InputCommandDispatcher::PointerEventBuffer::~PointerEventBuffer() {
  // Any remaining pointer events are dispatched to clients to keep a consistent
  // state:
  for (auto& pointer_id_and_streams : buffer_) {
    for (auto& stream : pointer_id_and_streams.second) {
      for (DeferredPerViewPointerEvents& views_and_events : stream.events) {
        MaybeDispatchFocusEvent(views_and_events, stream.focus_change);
        DispatchEvents(std::move(views_and_events));
      }
    }
  }
}

void InputCommandDispatcher::PointerEventBuffer::UpdateStream(
    uint32_t pointer_id, fuchsia::ui::input::accessibility::EventHandling handled) {
  auto it = buffer_.find(pointer_id);
  if (it == buffer_.end()) {
    // Empty buffer for this pointer id. Simply return.
    return;
  }
  auto& pointer_id_buffer = it->second;
  if (pointer_id_buffer.empty()) {
    // there are no streams left.
    return;
  }
  auto& stream = pointer_id_buffer.front();
  PointerIdStreamStatus status = PointerIdStreamStatus::WAITING_RESPONSE;
  switch (handled) {
    case fuchsia::ui::input::accessibility::EventHandling::CONSUMED:
      status = PointerIdStreamStatus::CONSUMED;
      break;
    case fuchsia::ui::input::accessibility::EventHandling::REJECTED:
      // the accessibility listener rejected this stream of pointer events
      // related to this pointer id. They follow their normal flow and are
      // sent to views. All buffered (past events), are sent, as well as
      // potential future (in case this stream is not done yet).
      status = PointerIdStreamStatus::REJECTED;
      for (DeferredPerViewPointerEvents& views_and_events : stream.events) {
        MaybeDispatchFocusEvent(views_and_events, stream.focus_change);
        DispatchEvents(std::move(views_and_events));
      }
      // Clears the stream -- objects have been moved, but container still holds
      // their space.
      stream.events.clear();
      break;
  };
  // Remove this stream from the buffer, as it was already processed.
  bool focus_change = stream.focus_change;  // record this before the stream goes away.
  pointer_id_buffer.pop_front();
  // If the buffer is now empty, this means that this stream hasn't finished
  // yet. Record this so that incoming future pointer events know where to go.
  // Please note that if the buffer is not empty, this means that there are
  // streams waiting for a response, thus, this is not the active stream
  // anymore. If this is the case, |active_stream_info_| will not be updated
  // and thus will still have a status of WAITING_RESPONSE.
  if (pointer_id_buffer.empty()) {
    SetActiveStreamInfo(pointer_id, status, focus_change);
  }
  FXL_DCHECK(pointer_id_buffer.empty() ||
             active_stream_info_[pointer_id].first == PointerIdStreamStatus::WAITING_RESPONSE)
      << "invariant: streams are waiting, so status is waiting";
}

void InputCommandDispatcher::PointerEventBuffer::AddEvents(
    uint32_t pointer_id, DeferredPerViewPointerEvents views_and_events,
    AccessibilityPointerEvent accessibility_pointer_event) {
  auto it = active_stream_info_.find(pointer_id);
  FXL_DCHECK(it != active_stream_info_.end()) << "Received an invalid pointer id.";
  const auto status = it->second.first;
  const bool focus_change = it->second.second;
  if (status == PointerIdStreamStatus::WAITING_RESPONSE) {
    PointerIdStream& stream = buffer_[pointer_id].back();
    stream.events.emplace_back(std::move(views_and_events));
  } else if (status == PointerIdStreamStatus::REJECTED) {
    // All previous events were already dispatched when this stream was
    // rejected. Sends this new incoming events to their normal flow as well.
    // There is still the possibility of triggering a focus change event, when
    // ADD -> a11y listener rejected -> DOWN event arrived.
    MaybeDispatchFocusEvent(views_and_events, focus_change);
    DispatchEvents(std::move(views_and_events));
    return;
  }
  // PointerIdStreamStatus::CONSUMED or PointerIdStreamStatus::WAITING_RESPONSE
  // follow the same path: accessibility listener needs to see the pointer event
  // to consume / decide if it will consume them.
  if (status == PointerIdStreamStatus::WAITING_RESPONSE ||
      status == PointerIdStreamStatus::CONSUMED) {
    auto listener_callback = [this](uint32_t device_id, uint32_t pointer_id,
                                    fuchsia::ui::input::accessibility::EventHandling handled) {
      this->UpdateStream(pointer_id, handled);
    };
    dispatcher_->input_system_->accessibility_pointer_event_listener()->OnEvent(
        std::move(accessibility_pointer_event), std::move(listener_callback));
  }
}

void InputCommandDispatcher::PointerEventBuffer::AddStream(uint32_t pointer_id, bool focus_change) {
  auto& pointer_id_buffer = buffer_[pointer_id];
  pointer_id_buffer.emplace_back();
  pointer_id_buffer.back().focus_change = focus_change;
  active_stream_info_[pointer_id] = {PointerIdStreamStatus::WAITING_RESPONSE, focus_change};
}

void InputCommandDispatcher::PointerEventBuffer::DispatchEvents(
    DeferredPerViewPointerEvents views_and_events) {
  for (auto& view_and_event : views_and_events) {
    dispatcher_->EnqueueEventToView(view_and_event.first, std::move(view_and_event.second));
  }
}

void InputCommandDispatcher::PointerEventBuffer::MaybeDispatchFocusEvent(
    const InputCommandDispatcher::PointerEventBuffer::DeferredPerViewPointerEvents&
        views_and_events,
    bool focus_change) {
  // If this parallel dispatch of events corresponds to a DOWN event, this
  // triggers a possible deferred focus change event.
  FXL_DCHECK(!views_and_events.empty()) << "Received an empty parallel dispatch of events.";
  const auto& event = views_and_events[0].second;
  if (event.phase == Phase::DOWN) {
    dispatcher_->MaybeChangeFocus(focus_change, views_and_events[0].first);
  }
}

}  // namespace input
}  // namespace scenic_impl
