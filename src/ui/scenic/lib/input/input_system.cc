// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_system.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <zircon/status.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/input/helper.h"

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

gfx::LayerStackPtr GetLayerStack(const gfx::SceneGraph& scene_graph, GlobalId compositor_id) {
  gfx::CompositorWeakPtr compositor = scene_graph.GetCompositor(compositor_id);
  FXL_DCHECK(compositor) << "No compositor, violated invariant.";

  gfx::LayerStackPtr layer_stack = compositor->layer_stack();
  FXL_DCHECK(layer_stack) << "No layer stack, violated invariant.";

  return layer_stack;
}

// The x and y values are in layer (screen) coordinates.
// NOTE: The accumulated hit structs contain resources that callers should let go of as soon as
// possible.
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

const char* InputSystem::kName = "InputSystem";

InputSystem::InputSystem(SystemContext context, fxl::WeakPtr<gfx::SceneGraph> scene_graph)
    : System(std::move(context)), scene_graph_(scene_graph) {
  FXL_CHECK(scene_graph);

  pointer_event_registry_ = std::make_unique<A11yPointerEventRegistry>(this->context());

  ime_service_ = this->context()->app_context()->svc()->Connect<fuchsia::ui::input::ImeService>();
  ime_service_.set_error_handler(
      [](zx_status_t status) { FXL_LOG(ERROR) << "Scenic lost connection to TextSync"; });

  this->context()->app_context()->outgoing()->AddPublicService(
      pointer_capture_registry_.GetHandler(this));

  FXL_LOG(INFO) << "Scenic input system initialized.";
}

CommandDispatcherUniquePtr InputSystem::CreateCommandDispatcher(
    scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
    std::shared_ptr<ErrorReporter> error_reporter) {
  return CommandDispatcherUniquePtr(
      new InputCommandDispatcher(session_id, std::move(event_reporter), scene_graph_, this),
      // Custom deleter.
      [](CommandDispatcher* cd) { delete cd; });
}

A11yPointerEventRegistry::A11yPointerEventRegistry(SystemContext* context) {
  context->app_context()->outgoing()->AddPublicService(
      accessibility_pointer_event_registry_.GetHandler(this));
}

void A11yPointerEventRegistry::Register(
    fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
        pointer_event_listener,
    RegisterCallback callback) {
  if (!accessibility_pointer_event_listener()) {
    accessibility_pointer_event_listener().Bind(std::move(pointer_event_listener));
    callback(/*success=*/true);
  } else {
    // An accessibility listener is already registered.
    callback(/*success=*/false);
  }
}

std::optional<glm::mat4> InputSystem::GetGlobalTransformByViewRef(
    const fuchsia::ui::views::ViewRef& view_ref) const {
  if (!scene_graph_) {
    return std::nullopt;
  }
  zx_koid_t view_ref_koid = fsl::GetKoid(view_ref.reference.get());
  return scene_graph_->view_tree().GlobalTransformOf(view_ref_koid);
}

void InputSystem::RegisterListener(
    fidl::InterfaceHandle<fuchsia::ui::input::PointerCaptureListener> listener_handle,
    fuchsia::ui::views::ViewRef view_ref, RegisterListenerCallback success_callback) {
  if (pointer_capture_listener_) {
    // Already have a listener, decline registration.
    success_callback(false);
    return;
  }

  fuchsia::ui::input::PointerCaptureListenerPtr new_listener;
  new_listener.Bind(std::move(listener_handle));

  // Remove listener if the interface closes.
  new_listener.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Pointer capture listener interface closed with error: "
                   << zx_status_get_string(status);
    pointer_capture_listener_ = std::nullopt;
  });

  pointer_capture_listener_ = PointerCaptureListener{.listener_ptr = std::move(new_listener),
                                                     .view_ref = std::move(view_ref)};

  success_callback(true);
}

void InputSystem::DispatchPointerCommand(const SendPointerInputCmd& command,
                                         scheduling::SessionId session_id, bool parallel_dispatch) {
  TRACE_DURATION("input", "dispatch_command", "command", "PointerCmd");
  if (!scene_graph_)
    return;

  // Compositor and layer stack required for dispatch.
  GlobalId compositor_id(session_id, command.compositor_id);
  gfx::CompositorWeakPtr compositor = scene_graph_->GetCompositor(compositor_id);
  if (!compositor)
    return;  // It's legal to race against GFX's compositor setup.

  gfx::LayerStackPtr layer_stack = compositor->layer_stack();
  if (!layer_stack)
    return;  // It's legal to race against GFX's layer stack setup.

  switch (command.pointer_event.type) {
    case PointerEventType::TOUCH:
      DispatchTouchCommand(command, layer_stack, session_id, parallel_dispatch,
                           /*a11y_enabled=*/ShouldForwardAccessibilityPointerEvents());
      break;
    case PointerEventType::MOUSE:
      DispatchMouseCommand(command, layer_stack);
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
void InputSystem::DispatchTouchCommand(const SendPointerInputCmd& command,
                                       const gfx::LayerStackPtr& layer_stack,
                                       scheduling::SessionId session_id, bool parallel_dispatch,
                                       bool a11y_enabled) {
  TRACE_DURATION("input", "dispatch_command", "command", "TouchCmd");
  trace_flow_id_t trace_id =
      PointerTraceHACK(command.pointer_event.radius_major, command.pointer_event.radius_minor);
  TRACE_FLOW_END("input", "dispatch_event_to_scenic", trace_id);

  const uint32_t pointer_id = command.pointer_event.pointer_id;
  const Phase pointer_phase = command.pointer_event.phase;
  const escher::vec2 pointer = PointerCoords(command.pointer_event);

  FXL_DCHECK(command.pointer_event.type == PointerEventType::TOUCH);
  FXL_DCHECK(pointer_phase != Phase::HOVER) << "Oops, touch device had unexpected HOVER event.";

  if (pointer_phase == Phase::ADD) {
    gfx::SessionHitAccumulator accumulator;
    PerformGlobalHitTest(layer_stack, pointer, &accumulator);
    const auto& hits = accumulator.hits();

    // Find input targets.  Honor the "input masking" view property.
    ViewStack hit_views;
    {
      // Find the transform (input -> view coordinates) for each hit and fill out hit_views.
      for (const gfx::ViewHit& hit : hits) {
        hit_views.stack.push_back({
            hit.view->view_ref_koid(),
            hit.view->event_reporter()->GetWeakPtr(),
            hit.screen_to_view_transform,
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
    if (!parallel_dispatch) {
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
    {
      // Find top-hit target and send it to accessibility.
      // NOTE: We may hit various mouse cursors (owned by root presenter), but |TopHitAccumulator|
      // will keep going until we find a hit with a valid owning View.
      gfx::TopHitAccumulator top_hit;
      PerformGlobalHitTest(layer_stack, pointer, &top_hit);

      if (top_hit.hit()) {
        const gfx::ViewHit& hit = *top_hit.hit();

        view_transform = hit.screen_to_view_transform;
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
         .parallel_event_receivers = std::move(deferred_event_receivers),
         .compositor_id = GlobalId{session_id, command.compositor_id}},
        std::move(packet));
  } else {
    // TODO(48150): Delete when we delete the PointerCapture functionality.
    ReportPointerEventToPointerCaptureListener(command.pointer_event,
                                               GlobalId{session_id, command.compositor_id});
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
void InputSystem::DispatchMouseCommand(const SendPointerInputCmd& command,
                                       const gfx::LayerStackPtr& layer_stack) {
  TRACE_DURATION("input", "dispatch_command", "command", "MouseCmd");

  const uint32_t device_id = command.pointer_event.device_id;
  const Phase pointer_phase = command.pointer_event.phase;
  const escher::vec2 pointer = PointerCoords(command.pointer_event);

  FXL_DCHECK(command.pointer_event.type == PointerEventType::MOUSE);
  FXL_DCHECK(pointer_phase != Phase::ADD && pointer_phase != Phase::REMOVE &&
             pointer_phase != Phase::HOVER)
      << "Oops, mouse device (id=" << device_id << ") had an unexpected event: " << pointer_phase;

  if (pointer_phase == Phase::DOWN) {
    // Find top-hit target and associated properties.
    // NOTE: We may hit various mouse cursors (owned by root presenter), but |TopHitAccumulator|
    // will keep going until we find a hit with a valid owning View.
    gfx::TopHitAccumulator top_hit;
    PerformGlobalHitTest(layer_stack, pointer, &top_hit);

    ViewStack hit_view;

    if (top_hit.hit()) {
      const gfx::ViewHit& hit = *top_hit.hit();

      hit_view.stack.push_back({
          hit.view->view_ref_koid(),
          hit.view->event_reporter()->GetWeakPtr(),
          hit.screen_to_view_transform,
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
    // Find top-hit target and send it this move event.
    // NOTE: We may hit various mouse cursors (owned by root presenter), but |TopHitAccumulator|
    // will keep going until we find a hit with a valid owning View.
    gfx::TopHitAccumulator top_hit;
    PerformGlobalHitTest(layer_stack, pointer, &top_hit);

    if (top_hit.hit()) {
      const gfx::ViewHit& hit = *top_hit.hit();

      ViewStack::Entry view_info;
      view_info.reporter = hit.view->event_reporter()->GetWeakPtr();
      view_info.transform = hit.screen_to_view_transform;
      PointerEvent clone;
      fidl::Clone(command.pointer_event, &clone);
      ReportPointerEvent(view_info, std::move(clone));
    }
  }
}

void InputSystem::DispatchDeferredPointerEvent(
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

  for (auto& view : views_and_event.parallel_event_receivers) {
    ReportPointerEvent(view, views_and_event.event);
  }

  {  // TODO(48150): Delete when we delete the PointerCapture functionality.
    ReportPointerEventToPointerCaptureListener(views_and_event.event,
                                               views_and_event.compositor_id);
  }
}

void InputSystem::ReportPointerEvent(const ViewStack::Entry& view_info,
                                     const PointerEvent& pointer) {
  if (!view_info.reporter)
    return;  // Session's event reporter no longer available. Bail quietly.

  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "pointer");
  trace_flow_id_t trace_id = PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
  TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", trace_id);

  InputEvent event;
  const auto transformed_coords =
      TransformPointerCoords(PointerCoords(pointer), view_info.transform);
  event.set_pointer(ClonePointerWithCoords(pointer, transformed_coords));

  view_info.reporter->EnqueueEvent(std::move(event));
}

zx_koid_t InputSystem::focus() const {
  if (!scene_graph_)
    return ZX_KOID_INVALID;  // No scene graph, no view tree, no focus chain.

  const auto& chain = scene_graph_->view_tree().focus_chain();
  if (chain.empty())
    return ZX_KOID_INVALID;  // Scene not present, or scene not connected to compositor.

  const zx_koid_t focused_view = chain.back();
  FXL_DCHECK(focused_view != ZX_KOID_INVALID) << "invariant";
  return focused_view;
}

zx_koid_t InputSystem::focus_chain_root() const {
  if (!scene_graph_)
    return ZX_KOID_INVALID;  // No scene graph, no view tree, no focus chain.

  const auto& chain = scene_graph_->view_tree().focus_chain();
  if (chain.empty())
    return ZX_KOID_INVALID;  // Scene not present, or scene not connected to compositor.

  const zx_koid_t root_view = chain.front();
  FXL_DCHECK(root_view != ZX_KOID_INVALID) << "invariant";
  return root_view;
}

void InputSystem::RequestFocusChange(zx_koid_t view) {
  FXL_DCHECK(view != ZX_KOID_INVALID) << "precondition";

  if (!scene_graph_)
    return;  // No scene graph, no view tree, no focus chain.

  if (scene_graph_->view_tree().focus_chain().empty())
    return;  // Scene not present, or scene not connected to compositor.

  // Input system acts on authority of top-most view.
  const zx_koid_t requestor = scene_graph_->view_tree().focus_chain()[0];

  auto status = scene_graph_->RequestFocusChange(requestor, view);
  FXL_VLOG(1) << "Scenic RequestFocusChange. Authority: " << requestor << ", request: " << view
              << ", status: " << static_cast<int>(status);

  FXL_DCHECK(status == FocusChangeStatus::kAccept ||
             status == FocusChangeStatus::kErrorRequestCannotReceiveFocus)
      << "User has authority to request focus change, but the only valid rejection is when the "
         "requested view may not receive focus. Error code: "
      << static_cast<int>(status);
}

bool InputSystem::ShouldForwardAccessibilityPointerEvents() {
  if (IsAccessibilityPointerEventForwardingEnabled()) {
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
          [this](fuchsia::ui::input::accessibility::PointerEvent pointer) {
            accessibility_pointer_event_listener()->OnEvent(std::move(pointer));
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
      accessibility_pointer_event_listener().events().OnStreamHandled =
          [buffer = pointer_event_buffer_.get()](
              uint32_t device_id, uint32_t pointer_id,
              fuchsia::ui::input::accessibility::EventHandling handled) {
            buffer->UpdateStream(pointer_id, handled);
          };
    }
    return true;
  } else if (pointer_event_buffer_) {
    // The listener disconnected. Release held events, delete the buffer.
    accessibility_pointer_event_listener().events().OnStreamHandled = nullptr;
    pointer_event_buffer_.reset();
  }
  return false;
}

// TODO(48150): Delete when we delete the PointerCapture functionality.
void InputSystem::ReportPointerEventToPointerCaptureListener(
    const fuchsia::ui::input::PointerEvent& pointer, GlobalId compositor_id) const {
  if (!pointer_capture_listener_ || !scene_graph_)
    return;

  const auto layers = GetLayerStack(*scene_graph_.get(), compositor_id)->layers();
  if (layers.empty())
    return;

  // Assume we only have one layer.
  const glm::mat4 screen_to_world_transform = (*layers.begin())->GetScreenToWorldSpaceTransform();

  const PointerCaptureListener& listener = pointer_capture_listener_.value();

  std::optional<glm::mat4> view_to_world_transform = GetGlobalTransformByViewRef(listener.view_ref);
  if (!view_to_world_transform)
    return;

  const auto world_to_view_transform = glm::inverse(view_to_world_transform.value());
  const auto screen_to_view_transform = world_to_view_transform * screen_to_world_transform;
  const auto local_coords =
      TransformPointerCoords(PointerCoords(pointer), screen_to_view_transform);
  const auto local_pointer = ClonePointerWithCoords(pointer, local_coords);

  // TODO(42145): Implement flow control.
  listener.listener_ptr->OnPointerEvent(std::move(local_pointer), [] {});
}

}  // namespace input
}  // namespace scenic_impl
