// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_system.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl {
namespace input {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using FocusChangeStatus = scenic_impl::gfx::ViewTree::FocusChangeStatus;
using Phase = fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;

namespace {

// Helper function to build an AccessibilityPointerEvent when there is a
// registered accessibility listener.
AccessibilityPointerEvent BuildAccessibilityPointerEvent(const PointerEvent& world_space_event,
                                                         const glm::vec2& ndc_point,
                                                         const glm::vec2& local_point,
                                                         uint64_t viewref_koid) {
  AccessibilityPointerEvent event;
  event.set_event_time(world_space_event.event_time);
  event.set_device_id(world_space_event.device_id);
  event.set_pointer_id(world_space_event.pointer_id);
  event.set_type(world_space_event.type);
  event.set_phase(world_space_event.phase);
  event.set_ndc_point({ndc_point.x, ndc_point.y});
  event.set_viewref_koid(viewref_koid);
  if (viewref_koid != ZX_KOID_INVALID) {
    event.set_local_point({local_point.x, local_point.y});
  }
  return event;
}

bool IsDescendantAndConnected(const gfx::ViewTree& view_tree, zx_koid_t descendant_koid,
                              zx_koid_t ancestor_koid) {
  if (!view_tree.IsTracked(descendant_koid) || !view_tree.IsTracked(ancestor_koid))
    return false;

  return view_tree.IsDescendant(descendant_koid, ancestor_koid) &&
         view_tree.IsConnectedToScene(ancestor_koid);
}

}  // namespace

const char* InputSystem::kName = "InputSystem";

InputSystem::InputSystem(SystemContext context, fxl::WeakPtr<gfx::SceneGraph> scene_graph)
    : System(std::move(context)), scene_graph_(scene_graph) {
  FX_CHECK(scene_graph);

  pointer_event_registry_ = std::make_unique<A11yPointerEventRegistry>(
      this->context(),
      /*on_register=*/
      [this] {
        FX_CHECK(!pointer_event_buffer_)
            << "on_disconnect must be called before registering a new listener";
        // In case a11y is turned on mid execution make sure to send active pointer event streams to
        // their final location and do not send them to the a11y listener.
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
        // Registers an event handler for this listener. This callback captures a pointer to the
        // event buffer that we own, so we need to clear it before we destroy it (see below).
        accessibility_pointer_event_listener().events().OnStreamHandled =
            [buffer = pointer_event_buffer_.get()](
                uint32_t device_id, uint32_t pointer_id,
                fuchsia::ui::input::accessibility::EventHandling handled) {
              buffer->UpdateStream(pointer_id, handled);
            };
      },
      /*on_disconnect=*/
      [this] {
        FX_CHECK(pointer_event_buffer_) << "can not disconnect before registering";
        // The listener disconnected. Release held events, delete the buffer.
        accessibility_pointer_event_listener().events().OnStreamHandled = nullptr;
        pointer_event_buffer_.reset();
      });

  ime_service_ = this->context()->app_context()->svc()->Connect<fuchsia::ui::input::ImeService>();
  ime_service_.set_error_handler(
      [](zx_status_t status) { FX_LOGS(ERROR) << "Scenic lost connection to TextSync"; });

  this->context()->app_context()->outgoing()->AddPublicService(injector_registry_.GetHandler(this));

  this->context()->app_context()->outgoing()->AddPublicService(
      pointer_capture_registry_.GetHandler(this));

  FX_LOGS(INFO) << "Scenic input system initialized.";
}

CommandDispatcherUniquePtr InputSystem::CreateCommandDispatcher(
    scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
    std::shared_ptr<ErrorReporter> error_reporter) {
  return CommandDispatcherUniquePtr(
      new InputCommandDispatcher(session_id, std::move(event_reporter), scene_graph_, this),
      // Custom deleter.
      [](CommandDispatcher* cd) { delete cd; });
}

A11yPointerEventRegistry::A11yPointerEventRegistry(SystemContext* context,
                                                   fit::function<void()> on_register,
                                                   fit::function<void()> on_disconnect)
    : on_register_(std::move(on_register)), on_disconnect_(std::move(on_disconnect)) {
  FX_DCHECK(on_register_);
  FX_DCHECK(on_disconnect_);
  context->app_context()->outgoing()->AddPublicService(
      accessibility_pointer_event_registry_.GetHandler(this));
}

void A11yPointerEventRegistry::Register(
    fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
        pointer_event_listener,
    RegisterCallback callback) {
  if (!accessibility_pointer_event_listener()) {
    accessibility_pointer_event_listener().Bind(std::move(pointer_event_listener));
    accessibility_pointer_event_listener_.set_error_handler(
        [this](zx_status_t) { on_disconnect_(); });
    on_register_();
    callback(/*success=*/true);
  } else {
    // An accessibility listener is already registered.
    callback(/*success=*/false);
  }
}

void InputSystem::Register(fuchsia::ui::pointerflow::InjectorConfig config,
                           fidl::InterfaceRequest<fuchsia::ui::pointerflow::Injector> injector,
                           RegisterCallback callback) {
  if (!config.has_device_config() || !config.has_context() || !config.has_target() ||
      !config.has_dispatch_policy()) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config| is incomplete.";
    return;
  }

  if (config.dispatch_policy() != fuchsia::ui::pointerflow::DispatchPolicy::EXCLUSIVE) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Only EXCLUSIVE DispatchPolicy is supported.";
    return;
  }

  if (!config.device_config().has_device_id() || !config.device_config().has_device_type()) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config.DeviceConfig| is incomplete.";
    return;
  }

  if (config.device_config().device_type() != fuchsia::ui::pointerflow::DeviceType::TOUCH) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Only TOUCH device type is supported.";
    return;
  }

  if (!config.context().is_view() || !config.target().is_view()) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config.context| or |config.target| "
                      "is incomplete.";
    return;
  }

  const zx_koid_t context_koid = utils::ExtractKoid(config.context().view());
  const zx_koid_t target_koid = utils::ExtractKoid(config.target().view());
  if (context_koid == ZX_KOID_INVALID || target_koid == ZX_KOID_INVALID) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config.context| or |config.target| "
                      "was invalid.";
    return;
  }
  if (!IsDescendantAndConnected(scene_graph_->view_tree(), target_koid, context_koid)) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config.context| must be connected to "
                      "the Scene, and |config.target| must be a descendant of |config.context|";
    return;
  }

  // TODO(50348): Add a callback to kill the channel immediately if connectivity breaks.

  const InjectorId id = ++last_injector_id_;
  InjectorSettings settings{.dispatch_policy = config.dispatch_policy(),
                            .device_id = config.device_config().device_id(),
                            .device_type = config.device_config().device_type(),
                            .context_koid = context_koid,
                            .target_koid = target_koid};
  const auto [it, success] = injectors_.try_emplace(
      id, id, std::move(settings), std::move(injector),
      /*is_descendant_and_connected*/
      [this](zx_koid_t descendant, zx_koid_t ancestor) {
        return IsDescendantAndConnected(scene_graph_->view_tree(), descendant, ancestor);
      },
      /*inject*/
      [this](zx_koid_t context, zx_koid_t target,
             const fuchsia::ui::input::PointerEvent& context_local_event) {
        InjectTouchEventExclusive(context_local_event, context, target);
      });
  FX_CHECK(success) << "Injector already exists.";

  // Remove the injector if the channel has an error.
  injectors_.at(id).SetErrorHandler([this, id](zx_status_t status) { injectors_.erase(id); });

  callback();
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
    FX_LOGS(ERROR) << "Pointer capture listener interface closed with error: "
                   << zx_status_get_string(status);
    pointer_capture_listener_ = std::nullopt;
  });

  pointer_capture_listener_ = PointerCaptureListener{.listener_ptr = std::move(new_listener),
                                                     .view_ref = std::move(view_ref)};

  success_callback(true);
}

void InputSystem::DispatchPointerCommand(const fuchsia::ui::input::SendPointerInputCmd& command,
                                         scheduling::SessionId session_id, bool parallel_dispatch) {
  TRACE_DURATION("input", "dispatch_command", "command", "PointerCmd");
  if (!scene_graph_)
    return;

  // Compositor and layer stack required for dispatch.
  const GlobalId compositor_id(session_id, command.compositor_id);
  gfx::CompositorWeakPtr compositor = scene_graph_->GetCompositor(compositor_id);
  if (!compositor)
    return;  // It's legal to race against GFX's compositor setup.

  gfx::LayerStackPtr layer_stack = compositor->layer_stack();
  if (!layer_stack)
    return;  // It's legal to race against GFX's layer stack setup.

  const auto layers = layer_stack->layers();
  if (layers.empty())
    return;

  // Assume we only have one layer.
  const glm::vec2 screen_space_coords = PointerCoords(command.pointer_event);
  const glm::mat4 screen_space_to_world_space_transform =
      (*layers.begin())->GetScreenToWorldSpaceTransform();
  const glm::vec2 world_space_coords =
      TransformPointerCoords(screen_space_coords, screen_space_to_world_space_transform);
  const fuchsia::ui::input::PointerEvent world_space_pointer_event =
      ClonePointerWithCoords(command.pointer_event, world_space_coords);

  switch (command.pointer_event.type) {
    case PointerEventType::TOUCH: {
      TRACE_DURATION("input", "dispatch_command", "command", "TouchCmd");
      trace_flow_id_t trace_id = PointerTraceHACK(world_space_pointer_event.radius_major,
                                                  world_space_pointer_event.radius_minor);
      TRACE_FLOW_END("input", "dispatch_event_to_scenic", trace_id);

      FX_DCHECK(world_space_pointer_event.type == PointerEventType::TOUCH);
      if (world_space_pointer_event.phase == Phase::HOVER) {
        FX_LOGS(WARNING) << "Oops, touch device had unexpected HOVER event.";
        return;
      }
      InjectTouchEventHitTested(world_space_pointer_event, screen_space_coords, layer_stack,
                                parallel_dispatch, IsA11yListenerEnabled());
      break;
    }
    case PointerEventType::MOUSE: {
      TRACE_DURATION("input", "dispatch_command", "command", "MouseCmd");
      if (command.pointer_event.phase == Phase::ADD ||
          command.pointer_event.phase == Phase::REMOVE ||
          command.pointer_event.phase == Phase::HOVER) {
        FX_LOGS(WARNING) << "Oops, mouse device (id=" << command.pointer_event.device_id
                         << ") had an unexpected event: " << command.pointer_event.phase;
        return;
      }
      InjectMouseEventHitTested(world_space_pointer_event, screen_space_coords, layer_stack);
      break;
    }
    default:
      // TODO(SCN-940), TODO(SCN-164): Stylus support needs to account for HOVER
      // events, which need to trigger an additional hit test on the DOWN event
      // and send CANCEL events to disassociated clients.
      FX_LOGS(INFO) << "Add stylus support.";
      break;
  }
}

void InputSystem::InjectTouchEventExclusive(
    const fuchsia::ui::input::PointerEvent& context_local_pointer_event, zx_koid_t context,
    zx_koid_t target) {
  if (!scene_graph_)
    return;

  std::optional<glm::mat4> context_to_world_transform = GetViewToWorldTransform(context);
  if (!context_to_world_transform)
    return;

  const glm::vec2 world_space_coords = TransformPointerCoords(
      PointerCoords(context_local_pointer_event), context_to_world_transform.value());
  const fuchsia::ui::input::PointerEvent world_space_pointer_event =
      ClonePointerWithCoords(context_local_pointer_event, world_space_coords);
  ReportPointerEventToView(world_space_pointer_event, target);
}

// The touch state machine comprises ADD/DOWN/MOVE*/UP/REMOVE. Some notes:
//  - We assume one touchscreen device, and use the device-assigned finger ID.
//  - Touch ADD associates the following ADD/DOWN/MOVE*/UP/REMOVE event sequence
//    with the set of clients available at that time. To enable gesture
//    disambiguation, we perform parallel dispatch to all clients.
//  - Touch DOWN triggers a focus change, honoring the "may receive focus" property.
//  - Touch REMOVE drops the association between event stream and client.
void InputSystem::InjectTouchEventHitTested(
    const fuchsia::ui::input::PointerEvent& world_space_pointer_event,
    const glm::vec2 screen_space_coords, const gfx::LayerStackPtr& layer_stack,
    bool parallel_dispatch, bool a11y_enabled) {
  FX_DCHECK(world_space_pointer_event.type == PointerEventType::TOUCH);
  const uint32_t pointer_id = world_space_pointer_event.pointer_id;
  const Phase pointer_phase = world_space_pointer_event.phase;

  if (pointer_phase == Phase::ADD) {
    gfx::ViewHitAccumulator accumulator;
    PerformGlobalHitTest(layer_stack, screen_space_coords, &accumulator);
    const auto& hits = accumulator.hits();

    // Find input targets.  Honor the "input masking" view property.
    std::vector<zx_koid_t> hit_views;
    for (const gfx::ViewHit& hit : hits) {
      hit_views.push_back(hit.view_ref_koid);
    }

    FX_VLOGS(1) << "View hits: ";
    for (auto view_ref_koid : hit_views) {
      FX_VLOGS(1) << "[ViewRefKoid=" << view_ref_koid << "]";
    }

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
      if (!touch_targets_[pointer_id].empty()) {
        // Request that focus be transferred to the top view.
        RequestFocusChange(touch_targets_[pointer_id].front());
      } else if (focus_chain_root() != ZX_KOID_INVALID) {
        // The touch event stream has no designated receiver.
        // Request that focus be transferred to the root view, so that (1) the currently focused
        // view becomes unfocused, and (2) the focus chain remains under control of the root view.
        RequestFocusChange(focus_chain_root());
      }
    }
  }

  // Input delivery must be parallel; needed for gesture disambiguation.
  std::vector<zx_koid_t> deferred_event_receivers;
  for (zx_koid_t view_ref_koid : touch_targets_[pointer_id]) {
    if (a11y_enabled) {
      deferred_event_receivers.emplace_back(view_ref_koid);
    } else {
      ReportPointerEventToView(world_space_pointer_event, view_ref_koid);
    }
    if (!parallel_dispatch) {
      break;  // TODO(SCN-1047): Remove when gesture disambiguation is ready.
    }
  }

  FX_DCHECK(a11y_enabled || deferred_event_receivers.empty())
      << "When a11y pointer forwarding is off, never defer events.";

  if (a11y_enabled) {
    // We handle both latched (!deferred_event_receivers.empty()) and unlatched
    // (deferred_event_receivers.empty()) touch events, for two reasons.
    //
    // (1) We must notify accessibility about events regardless of latch, so that it has full
    // information about a gesture stream. E.g., the gesture could start traversal in empty space
    // before MOVE-ing onto a rect; accessibility needs both the gesture and the rect.
    //
    // (2) We must trigger a potential focus change request, even if no view receives the triggering
    // DOWN event, so that (a) the focused view receives an unfocus event, and (b) the focus chain
    // gets updated and dispatched accordingly.
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
      PerformGlobalHitTest(layer_stack, screen_space_coords, &top_hit);

      if (top_hit.hit()) {
        view_ref_koid = top_hit.hit()->view_ref_koid;
      }
    }

    glm::vec2 top_hit_view_local;
    if (view_ref_koid != ZX_KOID_INVALID) {
      top_hit_view_local = TransformPointerCoords(PointerCoords(world_space_pointer_event),
                                                  GetWorldToViewTransform(view_ref_koid).value());
    }
    // TODO(50549): Still screen space dependent. Fix when a11y has its own view.
    const auto ndc = NormalizePointerCoords(screen_space_coords, layer_stack);

    AccessibilityPointerEvent packet = BuildAccessibilityPointerEvent(
        world_space_pointer_event, ndc, top_hit_view_local, view_ref_koid);
    pointer_event_buffer_->AddEvent(
        pointer_id,
        {.event = std::move(world_space_pointer_event),
         .parallel_event_receivers = std::move(deferred_event_receivers)},
        std::move(packet));
  } else {
    // TODO(48150): Delete when we delete the PointerCapture functionality.
    ReportPointerEventToPointerCaptureListener(world_space_pointer_event);
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
void InputSystem::InjectMouseEventHitTested(
    const fuchsia::ui::input::PointerEvent& world_space_pointer_event,
    glm::vec2 screen_space_coords, const gfx::LayerStackPtr& layer_stack) {
  FX_DCHECK(world_space_pointer_event.type == PointerEventType::MOUSE);
  const uint32_t device_id = world_space_pointer_event.device_id;
  const Phase pointer_phase = world_space_pointer_event.phase;
  const glm::vec2 pointer = PointerCoords(world_space_pointer_event);

  if (pointer_phase == Phase::DOWN) {
    // Find top-hit target and associated properties.
    // NOTE: We may hit various mouse cursors (owned by root presenter), but |TopHitAccumulator|
    // will keep going until we find a hit with a valid owning View.
    gfx::TopHitAccumulator top_hit;
    PerformGlobalHitTest(layer_stack, screen_space_coords, &top_hit);

    std::vector</*view_ref_koids*/ zx_koid_t> hit_views;
    if (top_hit.hit()) {
      hit_views.push_back(top_hit.hit()->view_ref_koid);
    }

    FX_VLOGS(1) << "View hits: ";
    for (auto view_ref_koid : hit_views) {
      FX_VLOGS(1) << "[ViewRefKoid=" << view_ref_koid << "]";
    }

    if (!hit_views.empty()) {
      // Request that focus be transferred to the top view.
      RequestFocusChange(hit_views.front());
    } else if (focus_chain_root() != ZX_KOID_INVALID) {
      // The mouse event stream has no designated receiver.
      // Request that focus be transferred to the root view, so that (1) the currently focused
      // view becomes unfocused, and (2) the focus chain remains under control of the root view.
      RequestFocusChange(focus_chain_root());
    }

    // Save target for consistent delivery of mouse events.
    mouse_targets_[device_id] = hit_views;
  }

  if (mouse_targets_.count(device_id) > 0 &&   // Tracking this device, and
      mouse_targets_[device_id].size() > 0) {  // target view exists.
    const zx_koid_t top_view_koid = mouse_targets_[device_id].front();
    ReportPointerEventToView(world_space_pointer_event, top_view_koid);
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
    PerformGlobalHitTest(layer_stack, screen_space_coords, &top_hit);

    if (top_hit.hit()) {
      const zx_koid_t top_view_koid = top_hit.hit()->view_ref_koid;
      ReportPointerEventToView(world_space_pointer_event, top_view_koid);
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
      const zx_koid_t view_koid = views_and_event.parallel_event_receivers.front();
      FX_DCHECK(view_koid != ZX_KOID_INVALID) << "invariant";
      RequestFocusChange(view_koid);
    } else if (focus_chain_root() != ZX_KOID_INVALID) {
      // The touch event stream has no designated receiver.
      // Request that focus be transferred to the root view, so that (1) the currently focused
      // view becomes unfocused, and (2) the focus chain remains under control of the root view.
      RequestFocusChange(focus_chain_root());
    }
  }

  for (zx_koid_t view_ref_koid : views_and_event.parallel_event_receivers) {
    ReportPointerEventToView(views_and_event.event, view_ref_koid);
  }

  {  // TODO(48150): Delete when we delete the PointerCapture functionality.
    ReportPointerEventToPointerCaptureListener(views_and_event.event);
  }
}

zx_koid_t InputSystem::focus() const {
  if (!scene_graph_)
    return ZX_KOID_INVALID;  // No scene graph, no view tree, no focus chain.

  const auto& chain = scene_graph_->view_tree().focus_chain();
  if (chain.empty())
    return ZX_KOID_INVALID;  // Scene not present, or scene not connected to compositor.

  const zx_koid_t focused_view = chain.back();
  FX_DCHECK(focused_view != ZX_KOID_INVALID) << "invariant";
  return focused_view;
}

zx_koid_t InputSystem::focus_chain_root() const {
  if (!scene_graph_)
    return ZX_KOID_INVALID;  // No scene graph, no view tree, no focus chain.

  const auto& chain = scene_graph_->view_tree().focus_chain();
  if (chain.empty())
    return ZX_KOID_INVALID;  // Scene not present, or scene not connected to compositor.

  const zx_koid_t root_view = chain.front();
  FX_DCHECK(root_view != ZX_KOID_INVALID) << "invariant";
  return root_view;
}

void InputSystem::RequestFocusChange(zx_koid_t view) {
  FX_DCHECK(view != ZX_KOID_INVALID) << "precondition";

  if (!scene_graph_)
    return;  // No scene graph, no view tree, no focus chain.

  if (scene_graph_->view_tree().focus_chain().empty())
    return;  // Scene not present, or scene not connected to compositor.

  // Input system acts on authority of top-most view.
  const zx_koid_t requestor = scene_graph_->view_tree().focus_chain().front();

  auto status = scene_graph_->RequestFocusChange(requestor, view);
  FX_VLOGS(1) << "Scenic RequestFocusChange. Authority: " << requestor << ", request: " << view
              << ", status: " << static_cast<int>(status);

  FX_DCHECK(status == FocusChangeStatus::kAccept ||
            status == FocusChangeStatus::kErrorRequestCannotReceiveFocus)
      << "User has authority to request focus change, but the only valid rejection is when the "
         "requested view may not receive focus. Error code: "
      << static_cast<int>(status);
}

// TODO(48150): Delete when we delete the PointerCapture functionality.
void InputSystem::ReportPointerEventToPointerCaptureListener(
    const fuchsia::ui::input::PointerEvent& world_space_pointer) const {
  if (!pointer_capture_listener_)
    return;

  const PointerCaptureListener& listener = pointer_capture_listener_.value();
  const zx_koid_t view_ref_koid = utils::ExtractKoid(listener.view_ref);
  std::optional<glm::mat4> world_to_view_transform = GetWorldToViewTransform(view_ref_koid);
  if (!world_to_view_transform)
    return;

  const glm::vec2 local_coords =
      TransformPointerCoords(PointerCoords(world_space_pointer), world_to_view_transform.value());

  // TODO(42145): Implement flow control.
  listener.listener_ptr->OnPointerEvent(ClonePointerWithCoords(world_space_pointer, local_coords),
                                        [] {});
}

void InputSystem::ReportPointerEventToView(
    const fuchsia::ui::input::PointerEvent& world_space_pointer, zx_koid_t view_ref_koid) const {
  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "pointer");
  EventReporterWeakPtr event_reporter = scene_graph_->view_tree().EventReporterOf(view_ref_koid);
  if (!event_reporter)
    return;

  std::optional<glm::mat4> world_to_view_transform = GetWorldToViewTransform(view_ref_koid);
  if (!world_to_view_transform)
    return;

  trace_flow_id_t trace_id =
      PointerTraceHACK(world_space_pointer.radius_major, world_space_pointer.radius_minor);
  TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", trace_id);

  const glm::vec2 local_coords =
      TransformPointerCoords(PointerCoords(world_space_pointer), world_to_view_transform.value());

  InputEvent event;
  event.set_pointer(ClonePointerWithCoords(world_space_pointer, local_coords));
  event_reporter->EnqueueEvent(std::move(event));
}

std::optional<glm::mat4> InputSystem::GetViewToWorldTransform(zx_koid_t view_ref_koid) const {
  FX_DCHECK(scene_graph_) << "precondition";
  return scene_graph_->view_tree().GlobalTransformOf(view_ref_koid);
}

std::optional<glm::mat4> InputSystem::GetWorldToViewTransform(zx_koid_t view_ref_koid) const {
  FX_DCHECK(scene_graph_) << "precondition";
  const auto view_to_world_transform = GetViewToWorldTransform(view_ref_koid);
  if (!view_to_world_transform)
    return std::nullopt;

  const glm::mat4 world_to_view_transform = glm::inverse(view_to_world_transform.value());
  return world_to_view_transform;
}

}  // namespace input
}  // namespace scenic_impl
