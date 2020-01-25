// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_command_dispatcher.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>

#include <memory>
#include <vector>

#include <trace/event.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"

namespace scenic_impl {
namespace input {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using FocusChangeStatus = scenic_impl::gfx::ViewTree::FocusChangeStatus;
using InputCommand = fuchsia::ui::input::Command;
using Phase = fuchsia::ui::input::PointerEventPhase;
using ScenicCommand = fuchsia::ui::scenic::Command;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::input::SendPointerInputCmd;

namespace {

// TODO(SCN-1278): Remove this.
// Turn two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

gfx::LayerStackPtr GetLayerStack(gfx::Engine* engine, GlobalId compositor_id) {
  FXL_DCHECK(engine);
  gfx::CompositorWeakPtr compositor = engine->scene_graph()->GetCompositor(compositor_id);
  FXL_DCHECK(compositor) << "No compositor, violated invariant.";

  gfx::LayerStackPtr layer_stack = compositor->layer_stack();
  FXL_DCHECK(layer_stack) << "No layer stack, violated invariant.";

  return layer_stack;
}

// The x and y values are in device (screen) coordinates.
// The initial dispatch logic guarantees a valid compositor and layer stack.
// NOTE: The accumulated hit structs contain resources that callers should let go of as soon as
// possible.
//
// Only the root presenter creates compositors and sends input commands.
// This invariant means this dispatcher context's session, handling an input
// command, also originally created the compositor.
//
void PerformGlobalHitTest(const gfx::LayerStackPtr& layer_stack, const escher::vec2& pointer,
                          gfx::HitAccumulator<gfx::ViewHit>* accumulator) {
  escher::ray4 ray = CreateScreenPerpendicularRay(pointer.x, pointer.y);
  FXL_VLOG(1) << "HitTest: device point (" << ray.origin.x << ", " << ray.origin.y << ")";

  layer_stack->HitTest(ray, accumulator);
}

// Helper function to build an AccessibilityPointerEvent when there is a
// registered accessibility listener.
AccessibilityPointerEvent BuildAccessibilityPointerEvent(const PointerEvent& original,
                                                         const escher::vec2& ndc_point,
                                                         const escher::vec2& local_point,
                                                         uint64_t viewref_koid) {
  AccessibilityPointerEvent event;
  event.set_event_time(original.event_time);
  event.set_device_id(original.device_id);
  event.set_pointer_id(original.pointer_id);
  event.set_type(original.type);
  event.set_phase(original.phase);
  event.set_ndc_point({ndc_point.x, ndc_point.y});
  event.set_viewref_koid(viewref_koid);
  if (viewref_koid != ZX_KOID_INVALID) {
    event.set_local_point({local_point.x, local_point.y});
  }
  return event;
}

}  // namespace

InputCommandDispatcher::InputCommandDispatcher(scheduling::SessionId session_id,
                                               std::shared_ptr<EventReporter> event_reporter,
                                               gfx::Engine* engine, InputSystem* input_system)
    : session_id_(session_id),
      event_reporter_(std::move(event_reporter)),
      engine_(engine),
      input_system_(input_system) {
  FXL_CHECK(engine_);
  FXL_CHECK(input_system_);
}

void InputCommandDispatcher::DispatchCommand(ScenicCommand command) {
  TRACE_DURATION("input", "dispatch_command", "command", "ScenicCmd");
  FXL_DCHECK(command.Which() == ScenicCommand::Tag::kInput);

  InputCommand& input = command.input();
  if (input.is_send_keyboard_input()) {
    DispatchCommand(input.send_keyboard_input());
  } else if (input.is_send_pointer_input()) {
    // Compositor and layer stack required for dispatch.
    GlobalId compositor_id(session_id_, input.send_pointer_input().compositor_id);
    gfx::CompositorWeakPtr compositor = engine_->scene_graph()->GetCompositor(compositor_id);
    if (!compositor)
      return;  // It's legal to race against GFX's compositor setup.

    gfx::LayerStackPtr layer_stack = compositor->layer_stack();
    if (!layer_stack)
      return;  // It's legal to race against GFX's layer stack setup.

    DispatchCommand(input.send_pointer_input());
  } else if (input.is_set_hard_keyboard_delivery()) {
    DispatchCommand(input.set_hard_keyboard_delivery());
  } else if (input.is_set_parallel_dispatch()) {
    DispatchCommand(input.set_parallel_dispatch());
  }
}

void InputCommandDispatcher::DispatchCommand(const SendPointerInputCmd& command) {
  TRACE_DURATION("input", "dispatch_command", "command", "PointerCmd");

  switch (command.pointer_event.type) {
    case PointerEventType::TOUCH:
      DispatchTouchCommand(command);
      break;
    case PointerEventType::MOUSE:
      DispatchMouseCommand(command);
      break;
    default:
      // TODO(SCN-940), TODO(SCN-164): Stylus support needs to account for HOVER
      // events, which need to trigger an additional hit test on the DOWN event
      // and send CANCEL events to disassociated clients.
      FXL_LOG(INFO) << "Add stylus support.";
      break;
  }
}

// The touch state machine comprises ADD/DOWN/MOVE*/UP/REMOVE. Some notes:
//  - We assume one touchscreen device, and use the device-assigned finger ID.
//  - Touch ADD associates the following ADD/DOWN/MOVE*/UP/REMOVE event sequence
//    with the set of clients available at that time. To enable gesture
//    disambiguation, we perform parallel dispatch to all clients.
//  - Touch DOWN triggers a focus change, honoring the "may receive focus" property.
//  - Touch REMOVE drops the association between event stream and client.
void InputCommandDispatcher::DispatchTouchCommand(const SendPointerInputCmd& command) {
  TRACE_DURATION("input", "dispatch_command", "command", "TouchCmd");
  trace_flow_id_t trace_id =
      PointerTraceHACK(command.pointer_event.radius_major, command.pointer_event.radius_minor);
  TRACE_FLOW_END("input", "dispatch_event_to_scenic", trace_id);

  const uint32_t pointer_id = command.pointer_event.pointer_id;
  const Phase pointer_phase = command.pointer_event.phase;
  const escher::vec2 pointer = PointerCoords(command.pointer_event);

  const bool a11y_enabled = ShouldForwardAccessibilityPointerEvents();

  FXL_DCHECK(command.pointer_event.type == PointerEventType::TOUCH);
  FXL_DCHECK(pointer_phase != Phase::HOVER) << "Oops, touch device had unexpected HOVER event.";

  if (pointer_phase == Phase::ADD) {
    GlobalId compositor_id(session_id_, command.compositor_id);

    gfx::SessionHitAccumulator accumulator;
    PerformGlobalHitTest(GetLayerStack(engine_, compositor_id), pointer, &accumulator);
    const auto& hits = accumulator.hits();

    // Find input targets.  Honor the "input masking" view property.
    ViewStack hit_views;
    {
      // Find the transform (input -> view coordinates) for each hit and fill out hit_views.
      for (const gfx::ViewHit& hit : hits) {
        hit_views.stack.push_back({
            hit.view->view_ref_koid(),
            hit.view->event_reporter()->GetWeakPtr(),
            hit.transform,
        });
        if (/*TODO(SCN-919): view_id may mask input */ false) {
          break;
        }
      }
    }
    FXL_VLOG(1) << "View stack of hits: " << hit_views;

    // Save targets for consistent delivery of touch events.
    touch_targets_[pointer_id] = hit_views;

    // If there is an accessibility pointer event listener enabled, an ADD event means that a new
    // pointer id stream started. Perform it unconditionally, even if the view stack is empty.
    if (a11y_enabled) {
      pointer_event_buffer_->AddStream(pointer_id);
    }
  } else if (pointer_phase == Phase::DOWN) {
    // If accessibility listener is on, focus change events must be sent only if
    // the stream is rejected. This way, this operation is deferred.
    if (!a11y_enabled) {
      if (!touch_targets_[pointer_id].stack.empty()) {
        // Request that focus be transferred to the top view.
        RequestFocusChange(touch_targets_[pointer_id].stack[0].view_ref_koid);
      } else if (focus_chain_root() != ZX_KOID_INVALID) {
        // The touch event stream has no designated receiver.
        // Request that focus be transferred to the root view, so that (1) the currently focused
        // view becomes unfocused, and (2) the focus chain remains under control of the root view.
        RequestFocusChange(focus_chain_root());
      }
    }
  }

  // Input delivery must be parallel; needed for gesture disambiguation.
  std::vector<ViewStack::Entry> deferred_event_receivers;
  for (const auto& entry : touch_targets_[pointer_id].stack) {
    if (a11y_enabled) {
      deferred_event_receivers.emplace_back(entry);
    } else {
      ReportPointerEvent(entry, command.pointer_event);
    }
    if (!parallel_dispatch_) {
      break;  // TODO(SCN-1047): Remove when gesture disambiguation is ready.
    }
  }

  FXL_DCHECK(a11y_enabled || deferred_event_receivers.empty())
      << "When a11y pointer forwarding is off, never defer events.";

  if (a11y_enabled) {
    // We handle both latched (!deferred_event_receivers.empty()) and unlatched
    // (deferred_event_receivers.empty()) touch events, for two reasons. (1) We must notify
    // accessibility about events regardless of latch, so that it has full
    //     information about a gesture stream. E.g., the gesture could start traversal in empty
    //     space before MOVE-ing onto a rect; accessibility needs both the gesture and the rect.
    // (2) We must trigger a potential focus change request, even if no view receives the triggering
    //     DOWN event, so that (a) the focused view receives an unfocus event, and (b) the focus
    //     chain gets updated and dispatched accordingly.
    //
    // NOTE: Do not rely on the latched view stack for "top hit" information; elevation can change
    // dynamically (it's only guaranteed correct for DOWN). Instead, perform an independent query
    // for "top hit".
    glm::mat4 view_transform(1.f);
    zx_koid_t view_ref_koid = ZX_KOID_INVALID;
    GlobalId compositor_id(session_id_, command.compositor_id);
    gfx::LayerStackPtr layer_stack = GetLayerStack(engine_, compositor_id);
    {
      // Find top-hit target and send it to accessibility.
      // NOTE: We may hit various mouse cursors (owned by root presenter), but |TopHitAccumulator|
      // will keep going until we find a hit with a valid owning View.
      gfx::TopHitAccumulator top_hit;
      PerformGlobalHitTest(layer_stack, pointer, &top_hit);

      if (top_hit.hit()) {
        const gfx::ViewHit& hit = *top_hit.hit();

        view_transform = hit.transform;
        view_ref_koid = hit.view->view_ref_koid();
      }
    }

    const auto ndc = NormalizePointerCoords(pointer, layer_stack);
    const auto top_hit_view_local = TransformPointerCoords(pointer, view_transform);
    AccessibilityPointerEvent packet = BuildAccessibilityPointerEvent(
        command.pointer_event, ndc, top_hit_view_local, view_ref_koid);
    pointer_event_buffer_->AddEvent(
        pointer_id,
        {.event = std::move(command.pointer_event),
         .parallel_event_receivers = std::move(deferred_event_receivers)},
        std::move(packet));
  } else {
    input_system_->ReportPointerEventToPointerCaptureListener(command.pointer_event);
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
//  - Mouse DOWN triggers a focus change, honoring the "may receive focus" property.
//  - Mouse UP drops the association between event stream and client.
//  - For an unlatched MOVE event, we perform a hit test, and send the
//    top-most client this MOVE event. Focus does not change for unlatched
//    MOVEs.
//  - The hit test must account for the mouse cursor itself, which today is
//    owned by the root presenter. The nodes associated with visible mouse
//    cursors(!) do not roll up to any View (as expected), but may appear in the
//    hit test; our dispatch needs to account for such behavior.
// TODO(SCN-1078): Enhance trackpad support.
void InputCommandDispatcher::DispatchMouseCommand(const SendPointerInputCmd& command) {
  TRACE_DURATION("input", "dispatch_command", "command", "MouseCmd");

  const uint32_t device_id = command.pointer_event.device_id;
  const Phase pointer_phase = command.pointer_event.phase;
  const escher::vec2 pointer = PointerCoords(command.pointer_event);

  FXL_DCHECK(command.pointer_event.type == PointerEventType::MOUSE);
  FXL_DCHECK(pointer_phase != Phase::ADD && pointer_phase != Phase::REMOVE &&
             pointer_phase != Phase::HOVER)
      << "Oops, mouse device (id=" << device_id << ") had an unexpected event: " << pointer_phase;

  if (pointer_phase == Phase::DOWN) {
    GlobalId compositor_id(session_id_, command.compositor_id);

    // Find top-hit target and associated properties.
    // NOTE: We may hit various mouse cursors (owned by root presenter), but |TopHitAccumulator|
    // will keep going until we find a hit with a valid owning View.
    gfx::TopHitAccumulator top_hit;
    PerformGlobalHitTest(GetLayerStack(engine_, compositor_id), pointer, &top_hit);

    ViewStack hit_view;

    if (top_hit.hit()) {
      const gfx::ViewHit& hit = *top_hit.hit();

      hit_view.stack.push_back({
          hit.view->view_ref_koid(),
          hit.view->event_reporter()->GetWeakPtr(),
          hit.transform,
      });
    }
    FXL_VLOG(1) << "View hit: " << hit_view;

    if (!hit_view.stack.empty()) {
      // Request that focus be transferred to the top view.
      RequestFocusChange(hit_view.stack[0].view_ref_koid);
    } else if (focus_chain_root() != ZX_KOID_INVALID) {
      // The mouse event stream has no designated receiver.
      // Request that focus be transferred to the root view, so that (1) the currently focused view
      // becomes unfocused, and (2) the focus chain remains under control of the root view.
      RequestFocusChange(focus_chain_root());
    }

    // Save target for consistent delivery of mouse events.
    mouse_targets_[device_id] = hit_view;
  }

  if (mouse_targets_.count(device_id) > 0 &&         // Tracking this device, and
      mouse_targets_[device_id].stack.size() > 0) {  // target view exists.
    const auto& entry = mouse_targets_[device_id].stack[0];
    PointerEvent clone;
    fidl::Clone(command.pointer_event, &clone);
    ReportPointerEvent(entry, std::move(clone));
  }

  if (pointer_phase == Phase::UP || pointer_phase == Phase::CANCEL) {
    mouse_targets_.erase(device_id);
  }

  // Deal with unlatched MOVE events.
  if (pointer_phase == Phase::MOVE && mouse_targets_.count(device_id) == 0) {
    GlobalId compositor_id(session_id_, command.compositor_id);

    // Find top-hit target and send it this move event.
    // NOTE: We may hit various mouse cursors (owned by root presenter), but |TopHitAccumulator|
    // will keep going until we find a hit with a valid owning View.
    gfx::TopHitAccumulator top_hit;
    PerformGlobalHitTest(GetLayerStack(engine_, compositor_id), pointer, &top_hit);

    if (top_hit.hit()) {
      const gfx::ViewHit& hit = *top_hit.hit();

      ViewStack::Entry view_info;
      view_info.reporter = hit.view->event_reporter()->GetWeakPtr();
      view_info.transform = hit.transform;
      PointerEvent clone;
      fidl::Clone(command.pointer_event, &clone);
      ReportPointerEvent(view_info, std::move(clone));
    }
  }
}

void InputCommandDispatcher::DispatchCommand(
    const fuchsia::ui::input::SendKeyboardInputCmd& command) {
  TRACE_DURATION("input", "dispatch_command", "command", "SendKeyboardInputCmd");
  // Expected (but soon to be deprecated) event flow.
  ReportToImeService(input_system_->ime_service(), command.keyboard_event);

  // Unusual: Clients may have requested direct delivery when focused.
  const zx_koid_t focused_view = focus();
  if (focused_view == ZX_KOID_INVALID)
    return;  // No receiver.

  const scenic_impl::gfx::ViewTree& view_tree = engine_->scene_graph()->view_tree();
  EventReporterWeakPtr reporter = view_tree.EventReporterOf(focused_view);
  scheduling::SessionId session_id = view_tree.SessionIdOf(focused_view);
  if (reporter && input_system_->hard_keyboard_requested().count(session_id) > 0) {
    ReportKeyboardEvent(reporter.get(), command.keyboard_event);
  }
}

void InputCommandDispatcher::DispatchCommand(
    const fuchsia::ui::input::SetHardKeyboardDeliveryCmd& command) {
  // Can't easily retrieve owning view's ViewRef KOID from just the Session or SessionId.
  FXL_VLOG(2) << "Hard keyboard events, session_id=" << session_id_
              << ", delivery_request=" << (command.delivery_request ? "on" : "off");

  if (command.delivery_request) {
    // Take this opportunity to remove dead sessions.
    std::vector<scheduling::SessionId> dead_sessions;

    for (auto& reporter : input_system_->hard_keyboard_requested()) {
      if (!reporter.second) {
        dead_sessions.push_back(reporter.first);
      }
    }
    for (auto session_ids : dead_sessions) {
      input_system_->hard_keyboard_requested().erase(session_id_);
    }

    // This code assumes one event reporter per session id.
    FXL_DCHECK(input_system_->hard_keyboard_requested().count(session_id_) == 0);
    if (event_reporter_)
      input_system_->hard_keyboard_requested().insert({session_id_, event_reporter_->GetWeakPtr()});
  } else {
    input_system_->hard_keyboard_requested().erase(session_id_);
  }
}

void InputCommandDispatcher::DispatchCommand(
    const fuchsia::ui::input::SetParallelDispatchCmd& command) {
  TRACE_DURATION("input", "dispatch_command", "command", "SetParallelDispatchCmd");
  FXL_LOG(INFO) << "Scenic: Parallel dispatch is turned "
                << (command.parallel_dispatch ? "ON" : "OFF");
  parallel_dispatch_ = command.parallel_dispatch;
}

void InputCommandDispatcher::DispatchDeferredPointerEvent(
    PointerEventBuffer::DeferredPointerEvent views_and_event) {
  // If this parallel dispatch of events corresponds to a DOWN event, this
  // triggers a possible deferred focus change event.
  if (views_and_event.event.phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
    if (!views_and_event.parallel_event_receivers.empty()) {
      // Request that focus be transferred to the top view.
      const zx_koid_t view_koid = views_and_event.parallel_event_receivers[0].view_ref_koid;
      FXL_DCHECK(view_koid != ZX_KOID_INVALID) << "invariant";
      RequestFocusChange(view_koid);
    } else if (focus_chain_root() != ZX_KOID_INVALID) {
      // The touch event stream has no designated receiver.
      // Request that focus be transferred to the root view, so that (1) the currently focused
      // view becomes unfocused, and (2) the focus chain remains under control of the root view.
      RequestFocusChange(focus_chain_root());
    }
  }

  input_system_->ReportPointerEventToPointerCaptureListener(views_and_event.event);
  for (auto& view : views_and_event.parallel_event_receivers) {
    ReportPointerEvent(view, views_and_event.event);
  }
}

void InputCommandDispatcher::ReportPointerEvent(const ViewStack::Entry& view_info,
                                                const PointerEvent& pointer) {
  if (!view_info.reporter)
    return;  // Session's event reporter no longer available. Bail quietly.

  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "pointer");
  trace_flow_id_t trace_id = PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
  TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", trace_id);

  InputEvent event;
  event.set_pointer(BuildLocalPointerEvent(pointer, view_info.transform));

  view_info.reporter->EnqueueEvent(std::move(event));
}

void InputCommandDispatcher::ReportKeyboardEvent(EventReporter* reporter,
                                                 fuchsia::ui::input::KeyboardEvent keyboard) {
  FXL_DCHECK(reporter) << "precondition";

  InputEvent event;
  event.set_keyboard(std::move(keyboard));
  reporter->EnqueueEvent(std::move(event));
}

void InputCommandDispatcher::ReportToImeService(
    const fuchsia::ui::input::ImeServicePtr& ime_service,
    fuchsia::ui::input::KeyboardEvent keyboard) {
  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "ime_keyboard_event");
  if (ime_service && ime_service.is_bound()) {
    InputEvent event;
    event.set_keyboard(std::move(keyboard));

    ime_service->InjectInput(std::move(event));
  }
}

zx_koid_t InputCommandDispatcher::focus() const {
  if (!engine_->scene_graph())
    return ZX_KOID_INVALID;  // No scene graph, no view tree, no focus chain.

  const auto& chain = engine_->scene_graph()->view_tree().focus_chain();
  if (chain.empty())
    return ZX_KOID_INVALID;  // Scene not present, or scene not connected to compositor.

  const zx_koid_t focused_view = chain.back();
  FXL_DCHECK(focused_view != ZX_KOID_INVALID) << "invariant";
  return focused_view;
}

zx_koid_t InputCommandDispatcher::focus_chain_root() const {
  if (!engine_->scene_graph())
    return ZX_KOID_INVALID;  // No scene graph, no view tree, no focus chain.

  const auto& chain = engine_->scene_graph()->view_tree().focus_chain();
  if (chain.empty())
    return ZX_KOID_INVALID;  // Scene not present, or scene not connected to compositor.

  const zx_koid_t root_view = chain.front();
  FXL_DCHECK(root_view != ZX_KOID_INVALID) << "invariant";
  return root_view;
}

bool InputCommandDispatcher::ShouldForwardAccessibilityPointerEvents() {
  if (input_system_->IsAccessibilityPointerEventForwardingEnabled()) {
    // If the buffer was not initialized yet, perform the following sanity
    // check: make sure to send active pointer event streams to their final
    // location and do not send them to the a11y listener.
    if (!pointer_event_buffer_) {
      pointer_event_buffer_ = std::make_unique<PointerEventBuffer>(
          /* DispatchEventFunction */
          [this](PointerEventBuffer::DeferredPointerEvent views_and_event) {
            DispatchDeferredPointerEvent(std::move(views_and_event));
          },
          /* ReportAccessibilityEventFunction */
          [input_system = input_system_](fuchsia::ui::input::accessibility::PointerEvent pointer) {
            input_system->accessibility_pointer_event_listener()->OnEvent(std::move(pointer));
          });

      for (const auto& kv : touch_targets_) {
        // Force a reject in all active pointer IDs. When a new stream arrives,
        // they will automatically be sent for the a11y listener decide
        // what to do with them as the status will change to WAITING_RESPONSE.
        pointer_event_buffer_->SetActiveStreamInfo(
            /*pointer_id=*/kv.first, PointerEventBuffer::PointerIdStreamStatus::REJECTED);
      }
      // Registers an event handler for this listener. This callback captures a pointer to the event
      // buffer that we own, so we need to clear it before we destroy it (see below).
      input_system_->accessibility_pointer_event_listener().events().OnStreamHandled =
          [buffer = pointer_event_buffer_.get()](
              uint32_t device_id, uint32_t pointer_id,
              fuchsia::ui::input::accessibility::EventHandling handled) {
            buffer->UpdateStream(pointer_id, handled);
          };
    }
    return true;
  } else if (pointer_event_buffer_) {
    // The listener disconnected. Release held events, delete the buffer.
    input_system_->accessibility_pointer_event_listener().events().OnStreamHandled = nullptr;
    pointer_event_buffer_.reset();
  }
  return false;
}

void InputCommandDispatcher::RequestFocusChange(zx_koid_t view) {
  FXL_DCHECK(view != ZX_KOID_INVALID) << "precondition";

  if (!engine_->scene_graph())
    return;  // No scene graph, no view tree, no focus chain.

  if (engine_->scene_graph()->view_tree().focus_chain().empty())
    return;  // Scene not present, or scene not connected to compositor.

  // Input system acts on authority of top-most view.
  const zx_koid_t requestor = engine_->scene_graph()->view_tree().focus_chain()[0];

  auto status = engine_->scene_graph()->RequestFocusChange(requestor, view);
  FXL_VLOG(1) << "Scenic RequestFocusChange. Authority: " << requestor << ", request: " << view
              << ", status: " << static_cast<int>(status);

  FXL_DCHECK(status == FocusChangeStatus::kAccept ||
             status == FocusChangeStatus::kErrorRequestCannotReceiveFocus)
      << "User has authority to request focus change, but the only valid rejection is when the "
         "requested view may not receive focus. Error code: "
      << static_cast<int>(status);
}

}  // namespace input
}  // namespace scenic_impl
