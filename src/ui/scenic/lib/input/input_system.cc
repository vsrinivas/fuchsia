// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_system.h"

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/sys/cpp/component_context.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <trace/event.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_point.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/util/type_utils.h"
#include "src/ui/scenic/lib/gfx/engine/hit.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/gfx/util/unwrap.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/session.h"

namespace scenic_impl {
namespace input {

const char* InputSystem::kName = "InputSystem";

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using FocusChangeStatus = scenic_impl::gfx::ViewTree::FocusChangeStatus;
using InputCommand = fuchsia::ui::input::Command;
using Phase = fuchsia::ui::input::PointerEventPhase;
using ScenicCommand = fuchsia::ui::scenic::Command;
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
using scenic_impl::gfx::ViewTree;

namespace {

// TODO(SCN-1278): Remove this.
// Turn two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

// View bound clipping is supposed to be exclusive, so to avoid spurious edge cases it's important
// to dispatch input events from their logical center. For discrete pixel coordinates, this would
// involve jittering it by (.5, .5). In addition, we floor the coordinate first in case the input
// device does support subpixel coordinates; if we wanted to center within the subpixel input
// resolution, we'd need to know the actual input resolution, which we don't, so just pretend
// they're always at pixel resolution.
void JitterPointerEvent(PointerEvent* pointer_event) {
  pointer_event->x = std::floor(pointer_event->x) + .5f;
  pointer_event->y = std::floor(pointer_event->y) + .5f;
}

// LINT.IfChange
// Helper for Dispatch[Touch|Mouse]Command and PerformGlobalHitTest.
escher::ray4 CreateScreenPerpendicularRay(float x, float y) {
  // We set the elevation for the origin point, and Z value for the direction,
  // such that we start above the scene and point into the scene.
  //
  // For hit testing, these values work in conjunction with
  // Camera::ProjectRayIntoScene to create an appropriate ray4 that works
  // correctly with the hit tester.
  //
  // TODO(38389): Scenic used to surface left-handed z, so |layer.cc| contains vestigial logic that
  // flips z. As such, "taking a step back" translates to "positive Z origin" and "look at the
  // scene" translates to "negative Z direction". We should be able to remove that flip and restore
  // Vulkan's z-in semantics. Similarly since hit testing originates from the camera it should not
  // be necessary to step back from the camera for the hit ray.
  //
  // During hit testing, we translate an arbitrary pointer's (x,y) device-space
  // coordinates to a View's (x', y') model-space coordinates.
  return {{x, y, 1, 1},  // Origin as homogeneous point.
          {0, 0, -1, 0}};
}
// LINT.ThenChange(//src/ui/scenic/lib/gfx/tests/hittest_global_unittest.cc)

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

  gfx::HitTester hit_tester;
  layer_stack->HitTest(ray, &hit_tester, accumulator);
}

// Helper for DispatchCommand.
PointerEvent ClonePointerWithCoords(const PointerEvent& event, const escher::vec2& coords) {
  PointerEvent clone;
  fidl::Clone(event, &clone);
  clone.x = coords.x;
  clone.y = coords.y;
  return clone;
}

escher::vec2 PointerCoords(const PointerEvent& event) { return {event.x, event.y}; }

// Helper for Dispatch[Touch|Mouse]Command.
escher::vec2 TransformPointerCoords(const escher::vec2& pointer, const glm::mat4 transform) {
  const escher::ray4 screen_ray = CreateScreenPerpendicularRay(pointer.x, pointer.y);
  const escher::ray4 local_ray = transform * screen_ray;

  // We treat distance as 0 to simplify; otherwise the formula is:
  // hit = homogenize(local_ray.origin + distance * local_ray.direction);
  escher::vec2 hit(escher::homogenize(local_ray.origin));

  FXL_VLOG(2) << "Coordinate transform (device->view): (" << screen_ray.origin.x << ", "
              << screen_ray.origin.y << ")->(" << hit.x << ", " << hit.y << ")";
  return hit;
}

// Finds (Vulkan) normalized device coordinates with respect to the (single) layer. This is intended
// for magnification gestures.
escher::vec2 NormalizePointerCoords(const escher::vec2& pointer,
                                    const gfx::LayerStackPtr& layer_stack) {
  if (layer_stack->layers().empty()) {
    return {0, 0};
  }

  // RootPresenter only owns one layer per presentation/layer stack. To support multiple layers,
  // we'd need to partition the input space. So, for now to simplify things we'll treat the layer
  // size as display dimensions, and if we ever find more than one layer in a stack, we should
  // worry.
  FXL_DCHECK(layer_stack->layers().size() == 1)
      << "Multiple GFX layers; multi-layer input dispatch not implemented.";
  const gfx::Layer& layer = **layer_stack->layers().begin();

  return {
      layer.width() > 0 ? 2.f * pointer.x / layer.width() - 1 : 0,
      layer.height() > 0 ? 2.f * pointer.y / layer.height() - 1 : 0,
  };
}

// Helper for EnqueueEventToView.
// Builds a pointer event with local view coordinates.
PointerEvent BuildLocalPointerEvent(const PointerEvent& pointer_event, const glm::mat4& transform) {
  return ClonePointerWithCoords(pointer_event,
                                TransformPointerCoords(PointerCoords(pointer_event), transform));
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

InputSystem::InputSystem(SystemContext context, gfx::Engine* engine)
    : System(std::move(context)), engine_(engine) {
  FXL_CHECK(engine_);
  ime_service_ = this->context()->app_context()->svc()->Connect<ImeService>();
  ime_service_.set_error_handler(
      [](zx_status_t status) { FXL_LOG(ERROR) << "Scenic lost connection to TextSync"; });

  this->context()->app_context()->outgoing()->AddPublicService(
      accessibility_pointer_event_registry_.GetHandler(this));

  FXL_LOG(INFO) << "Scenic input system initialized.";
}

CommandDispatcherUniquePtr InputSystem::CreateCommandDispatcher(CommandDispatcherContext context) {
  return CommandDispatcherUniquePtr(new InputCommandDispatcher(std::move(context), engine_, this),
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
                                               gfx::Engine* engine, InputSystem* input_system)
    : CommandDispatcher(std::move(command_dispatcher_context)),
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
    GlobalId compositor_id(command_dispatcher_context()->session_id(),
                           input.send_pointer_input().compositor_id);
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

  SendPointerInputCmd jittered = command;
  JitterPointerEvent(&jittered.pointer_event);

  switch (command.pointer_event.type) {
    case PointerEventType::TOUCH:
      DispatchTouchCommand(jittered);
      break;
    case PointerEventType::MOUSE:
      DispatchMouseCommand(jittered);
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
    GlobalId compositor_id(command_dispatcher_context()->session_id(), command.compositor_id);

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
  std::vector<std::pair<ViewStack::Entry, PointerEvent>> deferred_events;
  for (const auto& entry : touch_targets_[pointer_id].stack) {
    PointerEvent clone;
    fidl::Clone(command.pointer_event, &clone);
    if (a11y_enabled) {
      deferred_events.emplace_back(entry, std::move(clone));
    } else {
      ReportPointerEvent(entry, std::move(clone));
    }
    if (!parallel_dispatch_) {
      break;  // TODO(SCN-1047): Remove when gesture disambiguation is ready.
    }
  }

  FXL_DCHECK(a11y_enabled || deferred_events.empty())
      << "When a11y pointer forwarding is off, never defer events.";
  if (a11y_enabled) {
    // We handle both latched (!deferred_events.empty()) and unlatched (deferred_events.empty())
    // touch events, for two reasons.
    // (1) We must notify accessibility about events regardless of latch, so that it has full
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
    GlobalId compositor_id(command_dispatcher_context()->session_id(), command.compositor_id);
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
    pointer_event_buffer_->AddEvents(
        pointer_id, {.phase = pointer_phase, .parallel_events = std::move(deferred_events)},
        std::move(packet));
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
  const uint32_t device_id = command.pointer_event.device_id;
  const Phase pointer_phase = command.pointer_event.phase;
  const escher::vec2 pointer = PointerCoords(command.pointer_event);

  FXL_DCHECK(command.pointer_event.type == PointerEventType::MOUSE);
  FXL_DCHECK(pointer_phase != Phase::ADD && pointer_phase != Phase::REMOVE &&
             pointer_phase != Phase::HOVER)
      << "Oops, mouse device (id=" << device_id << ") had an unexpected event: " << pointer_phase;

  if (pointer_phase == Phase::DOWN) {
    GlobalId compositor_id(command_dispatcher_context()->session_id(), command.compositor_id);

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
    GlobalId compositor_id(command_dispatcher_context()->session_id(), command.compositor_id);

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

void InputCommandDispatcher::DispatchCommand(const SendKeyboardInputCmd& command) {
  // Expected (but soon to be deprecated) event flow.
  ReportToImeService(input_system_->ime_service(), command.keyboard_event);

  // Unusual: Clients may have requested direct delivery when focused.
  const zx_koid_t focused_view = focus();
  if (focused_view == ZX_KOID_INVALID)
    return;  // No receiver.

  const ViewTree& view_tree = engine_->scene_graph()->view_tree();
  EventReporterWeakPtr reporter = view_tree.EventReporterOf(focused_view);
  SessionId gfx_session_id = view_tree.SessionIdOf(focused_view);
  if (reporter && input_system_->hard_keyboard_requested().count(gfx_session_id) > 0) {
    ReportKeyboardEvent(reporter.get(), command.keyboard_event);
  }
}

void InputCommandDispatcher::DispatchCommand(const SetHardKeyboardDeliveryCmd& command) {
  // Can't easily retrieve owning view's ViewRef KOID from just the Session or SessionId.
  const SessionId session_id = command_dispatcher_context()->session_id();
  FXL_VLOG(2) << "Hard keyboard events, session_id=" << session_id
              << ", delivery_request=" << (command.delivery_request ? "on" : "off");

  if (command.delivery_request) {
    // Take this opportunity to remove dead sessions.
    //
    // TODO(SCN-1545): Add a test for removal, since commenting out this code resulted in all
    // code passing.
    for (auto& reporter : input_system_->hard_keyboard_requested()) {
      if (!reporter.second) {
        input_system_->hard_keyboard_requested().erase(reporter.first);
      }
    }

    // This code assumes one event reporter per session id.
    FXL_DCHECK(input_system_->hard_keyboard_requested().count(session_id) == 0);
    auto reporter = command_dispatcher_context()->session()->event_reporter().get();
    if (reporter)
      input_system_->hard_keyboard_requested().insert({session_id, reporter->GetWeakPtr()});
  } else {
    input_system_->hard_keyboard_requested().erase(session_id);
  }
}

void InputCommandDispatcher::DispatchCommand(const SetParallelDispatchCmd& command) {
  FXL_LOG(INFO) << "Scenic: Parallel dispatch is turned "
                << (command.parallel_dispatch ? "ON" : "OFF");
  parallel_dispatch_ = command.parallel_dispatch;
}

void InputCommandDispatcher::ReportPointerEvent(const ViewStack::Entry& view_info,
                                                PointerEvent pointer) {
  if (!view_info.reporter)
    return;  // Session's event reporter no longer available. Bail quietly.

  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "pointer");
  trace_flow_id_t trace_id = PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
  TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", trace_id);

  InputEvent event;
  event.set_pointer(BuildLocalPointerEvent(pointer, view_info.transform));

  view_info.reporter->EnqueueEvent(std::move(event));
}

void InputCommandDispatcher::ReportKeyboardEvent(EventReporter* reporter, KeyboardEvent keyboard) {
  FXL_DCHECK(reporter) << "precondition";

  InputEvent event;
  event.set_keyboard(std::move(keyboard));
  reporter->EnqueueEvent(std::move(event));
}

void InputCommandDispatcher::ReportToImeService(const ImeServicePtr& ime_service,
                                                KeyboardEvent keyboard) {
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
      pointer_event_buffer_ = std::make_unique<PointerEventBuffer>(this);
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

InputCommandDispatcher::PointerEventBuffer::PointerEventBuffer(InputCommandDispatcher* dispatcher)
    : dispatcher_(dispatcher) {
  FXL_DCHECK(dispatcher_) << "Dispatcher can't be NULL.";
}

InputCommandDispatcher::PointerEventBuffer::~PointerEventBuffer() {
  // Any remaining pointer events are dispatched to clients to keep a consistent state.
  for (auto& pointer_id_and_streams : buffer_) {
    for (auto& stream : pointer_id_and_streams.second) {
      for (auto& deferred_events : stream.serial_events) {
        DispatchEvents(std::move(deferred_events));
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
      for (auto& deferred_events : stream.serial_events) {
        DispatchEvents(std::move(deferred_events));
      }
      // Clears the stream -- objects have been moved, but container still holds
      // their space.
      stream.serial_events.clear();
      break;
  };
  // Remove this stream from the buffer, as it was already processed.
  pointer_id_buffer.pop_front();
  // If the buffer is now empty, this means that this stream hasn't finished
  // yet. Record this so that incoming future pointer events know where to go.
  // Please note that if the buffer is not empty, this means that there are
  // streams waiting for a response, thus, this is not the active stream
  // anymore. If this is the case, |active_stream_info_| will not be updated
  // and thus will still have a status of WAITING_RESPONSE.
  if (pointer_id_buffer.empty()) {
    SetActiveStreamInfo(pointer_id, status);
  }
  FXL_DCHECK(pointer_id_buffer.empty() ||
             active_stream_info_[pointer_id] == PointerIdStreamStatus::WAITING_RESPONSE)
      << "invariant: streams are waiting, so status is waiting";
}

void InputCommandDispatcher::PointerEventBuffer::AddEvents(
    uint32_t pointer_id, DeferredPerViewPointerEvents views_and_events,
    AccessibilityPointerEvent accessibility_pointer_event) {
  auto it = active_stream_info_.find(pointer_id);
  FXL_DCHECK(it != active_stream_info_.end()) << "Received an invalid pointer id.";
  const auto status = it->second;
  if (status == PointerIdStreamStatus::WAITING_RESPONSE) {
    PointerIdStream& stream = buffer_[pointer_id].back();
    stream.serial_events.emplace_back(std::move(views_and_events));
  } else if (status == PointerIdStreamStatus::REJECTED) {
    // All previous events were already dispatched when this stream was
    // rejected. Sends this new incoming events to their normal flow as well.
    // There is still the possibility of triggering a focus change event, when
    // ADD -> a11y listener rejected -> DOWN event arrived.
    DispatchEvents(std::move(views_and_events));
    return;
  }
  // PointerIdStreamStatus::CONSUMED or PointerIdStreamStatus::WAITING_RESPONSE
  // follow the same path: accessibility listener needs to see the pointer event
  // to consume / decide if it will consume them.
  if (status == PointerIdStreamStatus::WAITING_RESPONSE ||
      status == PointerIdStreamStatus::CONSUMED) {
    dispatcher_->input_system_->accessibility_pointer_event_listener()->OnEvent(
        std::move(accessibility_pointer_event));
  }
}

void InputCommandDispatcher::PointerEventBuffer::AddStream(uint32_t pointer_id) {
  auto& pointer_id_buffer = buffer_[pointer_id];
  pointer_id_buffer.emplace_back();
  active_stream_info_[pointer_id] = PointerIdStreamStatus::WAITING_RESPONSE;
}

void InputCommandDispatcher::PointerEventBuffer::DispatchEvents(
    DeferredPerViewPointerEvents views_and_events) {
  // If this parallel dispatch of events corresponds to a DOWN event, this
  // triggers a possible deferred focus change event.
  if (views_and_events.phase == Phase::DOWN) {
    if (!views_and_events.parallel_events.empty()) {
      // Request that focus be transferred to the top view.
      FXL_DCHECK(views_and_events.parallel_events[0].second.phase == views_and_events.phase)
          << "invariant";
      const zx_koid_t view_koid = views_and_events.parallel_events[0].first.view_ref_koid;
      FXL_DCHECK(view_koid != ZX_KOID_INVALID) << "invariant";
      dispatcher_->RequestFocusChange(view_koid);
    } else if (dispatcher_->focus_chain_root() != ZX_KOID_INVALID) {
      // The touch event stream has no designated receiver.
      // Request that focus be transferred to the root view, so that (1) the currently focused
      // view becomes unfocused, and (2) the focus chain remains under control of the root view.
      dispatcher_->RequestFocusChange(dispatcher_->focus_chain_root());
    }
  }

  for (auto& view_and_event : views_and_events.parallel_events) {
    dispatcher_->ReportPointerEvent(view_and_event.first, std::move(view_and_event.second));
  }
}

}  // namespace input
}  // namespace scenic_impl
