// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_system.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/ui/input/accessibility/formatting.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/fostr/fidl/fuchsia/ui/pointerinjector/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/ui/lib/glm_workaround/glm_workaround.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/input/constants.h"
#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/input/internal_pointer_event.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl {
namespace input {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using FocusChangeStatus = scenic_impl::gfx::ViewTree::FocusChangeStatus;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;

namespace {

uint64_t NextTraceId() {
  static uint64_t next_trace_id = 1;
  return next_trace_id++;
}

// Creates a hit ray at z = -1000, pointing in the z-direction.
escher::ray4 CreateZRay(glm::vec2 coords) {
  return {
      // Origin as homogeneous point.
      .origin = {coords.x, coords.y, -1000, 1},
      .direction = {0, 0, 1, 0},
  };
}

bool IsOutsideViewport(const Viewport& viewport, const glm::vec2& position_in_viewport) {
  FX_DCHECK(!std::isunordered(position_in_viewport.x, viewport.extents.min.x) &&
            !std::isunordered(position_in_viewport.x, viewport.extents.max.x) &&
            !std::isunordered(position_in_viewport.y, viewport.extents.min.y) &&
            !std::isunordered(position_in_viewport.y, viewport.extents.max.y));
  return position_in_viewport.x < viewport.extents.min.x ||
         position_in_viewport.y < viewport.extents.min.y ||
         position_in_viewport.x > viewport.extents.max.x ||
         position_in_viewport.y > viewport.extents.max.y;
}

// Helper function to build an AccessibilityPointerEvent when there is a
// registered accessibility listener.
AccessibilityPointerEvent BuildAccessibilityPointerEvent(const InternalPointerEvent& internal_event,
                                                         const glm::vec2& ndc_point,
                                                         const glm::vec2& local_point,
                                                         uint64_t viewref_koid) {
  AccessibilityPointerEvent event;
  event.set_event_time(internal_event.timestamp);
  event.set_device_id(internal_event.device_id);
  event.set_pointer_id(internal_event.pointer_id);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(InternalPhaseToGfxPhase(internal_event.phase));
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

std::optional<glm::mat4> GetWorldFromViewTransform(zx_koid_t view_ref_koid,
                                                   const gfx::ViewTree& view_tree) {
  return view_tree.GlobalTransformOf(view_ref_koid);
}

std::optional<glm::mat4> GetViewFromWorldTransform(zx_koid_t view_ref_koid,
                                                   const gfx::ViewTree& view_tree) {
  const auto world_from_view_transform = GetWorldFromViewTransform(view_ref_koid, view_tree);
  if (!world_from_view_transform)
    return std::nullopt;

  const glm::mat4 view_from_world_transform = glm::inverse(world_from_view_transform.value());
  return view_from_world_transform;
}

std::optional<glm::mat4> GetDestinationViewFromSourceViewTransform(zx_koid_t source,
                                                                   zx_koid_t destination,
                                                                   const gfx::ViewTree& view_tree) {
  std::optional<glm::mat4> world_from_source_transform =
      GetWorldFromViewTransform(source, view_tree);

  if (!world_from_source_transform)
    return std::nullopt;

  std::optional<glm::mat4> destination_from_world_transform =
      GetViewFromWorldTransform(destination, view_tree);
  if (!destination_from_world_transform)
    return std::nullopt;

  return destination_from_world_transform.value() * world_from_source_transform.value();
}

escher::ray4 CreateWorldSpaceRay(const InternalPointerEvent& event,
                                 const gfx::ViewTree& view_tree) {
  const std::optional<glm::mat4> world_from_context_transform =
      GetWorldFromViewTransform(event.context, view_tree);
  FX_DCHECK(world_from_context_transform)
      << "Failed to create world space ray. Either the |event.context| ViewRef is invalid, we're "
         "out of sync with the ViewTree, or the ViewTree callback returned std::nullopt.";

  const glm::mat4 world_from_viewport_transform =
      world_from_context_transform.value() * event.viewport.context_from_viewport_transform;
  const escher::ray4 viewport_space_ray = CreateZRay(event.position_in_viewport);
  return world_from_viewport_transform * viewport_space_ray;
}

// Takes an InternalPointerEvent and returns a point in (Vulkan) Normalized Device Coordinates,
// in relation to the viewport. Intended for magnification
// TODO(fxbug.dev/50549): Only here to allow the legacy a11y flow. Remove along with the legacy a11y
// code.
glm::vec2 GetViewportNDCPoint(const InternalPointerEvent& internal_event) {
  const float width = internal_event.viewport.extents.max.x - internal_event.viewport.extents.min.x;
  const float height =
      internal_event.viewport.extents.max.y - internal_event.viewport.extents.min.y;
  return {
      width > 0 ? 2.f * internal_event.position_in_viewport.x / width - 1 : 0,
      height > 0 ? 2.f * internal_event.position_in_viewport.y / height - 1 : 0,
  };
}

void ChattyGfxLog(const fuchsia::ui::input::InputEvent& event) {
  static uint32_t chatty = 0;
  if (chatty++ < ChattyMax()) {
    FX_LOGS(INFO) << "Ptr-GFX[" << chatty << "/" << ChattyMax() << "]: " << event;
  }
}

void ChattyCaptureLog(const fuchsia::ui::input::PointerEvent& event) {
  static uint32_t chatty = 0;
  if (chatty++ < ChattyMax()) {
    FX_LOGS(INFO) << "Ptr-Capture[" << chatty << "/" << ChattyMax() << "]: " << event;
  }
}

void ChattyA11yLog(const fuchsia::ui::input::accessibility::PointerEvent& event) {
  static uint32_t chatty = 0;
  if (chatty++ < ChattyMax()) {
    FX_LOGS(INFO) << "Ptr-A11y[" << chatty << "/" << ChattyMax() << "]: " << event;
  }
}

}  // namespace

const char* InputSystem::kName = "InputSystem";

InputSystem::InputSystem(SystemContext context, fxl::WeakPtr<gfx::SceneGraph> scene_graph,
                         bool pointer_auto_focus)
    : System(std::move(context)),
      pointer_auto_focus_(pointer_auto_focus),
      scene_graph_(scene_graph) {
  FX_CHECK(scene_graph);

  pointer_event_registry_ = std::make_unique<A11yPointerEventRegistry>(
      this->context(),
      /*on_register=*/
      [this] {
        FX_CHECK(!a11y_legacy_contender_)
            << "on_disconnect must be called before registering a new listener";

        a11y_legacy_contender_ = std::make_unique<A11yLegacyContender>(
            /*respond*/
            [this](StreamId stream_id, GestureResponse response) {
              RecordGestureDisambiguationResponse(stream_id, a11y_contender_id_, {response});
            },
            /*deliver_to_client*/
            [this](const InternalPointerEvent& event) {
              if (!scene_graph_)
                return;

              const gfx::ViewTree& view_tree = scene_graph_->view_tree();
              auto a11y_event = CreateAccessibilityEvent(event, view_tree);
              ChattyA11yLog(a11y_event);
              accessibility_pointer_event_listener()->OnEvent(std::move(a11y_event));
            });
        FX_LOGS(INFO) << "A11yLegacyContender created.";
        contenders_.emplace(a11y_contender_id_, a11y_legacy_contender_.get());

        accessibility_pointer_event_listener().events().OnStreamHandled =
            [this](uint32_t device_id, uint32_t pointer_id,
                   fuchsia::ui::input::accessibility::EventHandling handled) {
              FX_DCHECK(a11y_legacy_contender_);
              a11y_legacy_contender_->OnStreamHandled(pointer_id, handled);
            };
      },
      /*on_disconnect=*/
      [this] {
        FX_CHECK(a11y_legacy_contender_) << "can not disconnect before registering";
        // The listener disconnected. Release held events, delete the buffer.
        accessibility_pointer_event_listener().events().OnStreamHandled = nullptr;
        contenders_.erase(a11y_contender_id_);
        a11y_legacy_contender_.reset();
        FX_LOGS(INFO) << "A11yLegacyContender destroyed";
      });

  this->context()->app_context()->outgoing()->AddPublicService(injector_registry_.GetHandler(this));

  this->context()->app_context()->outgoing()->AddPublicService(
      pointer_capture_registry_.GetHandler(this));

  FX_LOGS(INFO) << "Scenic input system initialized.";
}

CommandDispatcherUniquePtr InputSystem::CreateCommandDispatcher(
    scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
    std::shared_ptr<ErrorReporter> error_reporter) {
  return CommandDispatcherUniquePtr(new InputCommandDispatcher(session_id, this),
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
    accessibility_pointer_event_listener_.Bind(std::move(pointer_event_listener));
    accessibility_pointer_event_listener_.set_error_handler(
        [this](zx_status_t) { on_disconnect_(); });
    on_register_();
    callback(/*success=*/true);
  } else {
    // An accessibility listener is already registered.
    callback(/*success=*/false);
  }
}

fuchsia::ui::input::accessibility::PointerEvent InputSystem::CreateAccessibilityEvent(
    const InternalPointerEvent& event, const gfx::ViewTree& view_tree) {
  zx_koid_t view_ref_koid = ZX_KOID_INVALID;
  {
    // Find top-hit target and send it to accessibility.
    gfx::TopHitAccumulator top_hit;
    HitTest(view_tree, event, top_hit, /*semantic_hit_test*/ true);

    if (top_hit.hit()) {
      view_ref_koid = top_hit.hit()->view_ref_koid;
    }
  }

  glm::vec2 top_hit_view_local;
  if (view_ref_koid != ZX_KOID_INVALID) {
    std::optional<glm::mat4> view_from_context = GetDestinationViewFromSourceViewTransform(
        /*source*/ event.context, /*destination*/ view_ref_koid, view_tree);
    FX_DCHECK(view_from_context)
        << "Failed to create world space ray. Either the |event.context| ViewRef is invalid, "
           "we're out of sync with the ViewTree, or the ViewTree callback returned std::nullopt.";

    const glm::mat4 view_from_viewport =
        view_from_context.value() * event.viewport.context_from_viewport_transform;
    top_hit_view_local = TransformPointerCoords(event.position_in_viewport, view_from_viewport);
  }
  const glm::vec2 ndc = GetViewportNDCPoint(event);

  return BuildAccessibilityPointerEvent(event, ndc, top_hit_view_local, view_ref_koid);
}

void InputSystem::Register(fuchsia::ui::pointerinjector::Config config,
                           fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> injector,
                           RegisterCallback callback) {
  if (!Injector::IsValidConfig(config)) {
    // Errors printed inside IsValidConfig. Just return here.
    return;
  }

  // Check connectivity here, since injector doesn't have access to it.
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

  // TODO(fxbug.dev/50348): Add a callback to kill the channel immediately if connectivity breaks.

  const InjectorId id = ++last_injector_id_;
  InjectorSettings settings{.dispatch_policy = config.dispatch_policy(),
                            .device_id = config.device_id(),
                            .device_type = config.device_type(),
                            .context_koid = context_koid,
                            .target_koid = target_koid};
  Viewport viewport{
      .extents = {config.viewport().extents()},
      .context_from_viewport_transform =
          ColumnMajorMat3VectorToMat4(config.viewport().viewport_to_context_transform()),
  };

  fit::function<void(const InternalPointerEvent&, StreamId)> inject_func;
  switch (settings.dispatch_policy) {
    case fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET:
      inject_func = [this](const InternalPointerEvent& event, StreamId stream_id) {
        InjectTouchEventExclusive(event);
      };
      break;
    case fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET:
      inject_func = [this](const InternalPointerEvent& event, StreamId stream_id) {
        InjectTouchEventHitTested(event, stream_id);
      };
      break;
    default:
      FX_CHECK(false) << "Should never be reached.";
      break;
  }

  const auto [it, success] = injectors_.try_emplace(
      id,
      context()->inspect_node()->CreateChild(context()->inspect_node()->UniqueName("injector-")),
      std::move(settings), std::move(viewport), std::move(injector),
      /*is_descendant_and_connected*/
      [this](zx_koid_t descendant, zx_koid_t ancestor) {
        return IsDescendantAndConnected(scene_graph_->view_tree(), descendant, ancestor);
      },
      std::move(inject_func),
      /*on_channel_closed*/
      [this, id] { injectors_.erase(id); });
  FX_CHECK(success) << "Injector already exists.";

  callback();
}

ContenderId InputSystem::AddGfxLegacyContender(StreamId stream_id, zx_koid_t view_ref_koid) {
  FX_DCHECK(view_ref_koid != ZX_KOID_INVALID);

  const ContenderId contender_id = next_contender_id_++;
  gfx_legacy_contenders_.try_emplace(
      contender_id,
      /*respond*/
      [this, stream_id, contender_id](GestureResponse response) {
        RecordGestureDisambiguationResponse(stream_id, contender_id, {response});
      },
      /*deliver_events_to_client*/
      [this, view_ref_koid](const std::vector<InternalPointerEvent>& events) {
        if (!scene_graph_)
          return;

        const gfx::ViewTree& view_tree = scene_graph_->view_tree();
        for (const auto& event : events) {
          ReportPointerEventToPointerCaptureListener(event, view_tree);
          ReportPointerEventToGfxLegacyView(event, view_ref_koid,
                                            fuchsia::ui::input::PointerEventType::TOUCH, view_tree);

          // Update focus if necessary.
          // TODO(fxbug.dev/59858): Figure out how to handle focus with real GD clients.
          if (event.phase == Phase::kAdd) {
            if (view_tree.IsConnectedToScene(view_ref_koid)) {
              if (view_tree.MayReceiveFocus(view_ref_koid)) {
                RequestFocusChange(view_ref_koid);
              }
            } else if (focus_chain_root() != ZX_KOID_INVALID) {
              RequestFocusChange(focus_chain_root());
            }
          }
        }
      },
      /*self_destruct*/
      [this, contender_id] {
        contenders_.erase(contender_id);
        gfx_legacy_contenders_.erase(contender_id);
      });
  contenders_.emplace(contender_id, &gfx_legacy_contenders_.at(contender_id));
  return contender_id;
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

void InputSystem::HitTest(const gfx::ViewTree& view_tree, const InternalPointerEvent& event,
                          gfx::HitAccumulator<gfx::ViewHit>& accumulator,
                          bool semantic_hit_test) const {
  if (IsOutsideViewport(event.viewport, event.position_in_viewport)) {
    return;
  }

  escher::ray4 world_ray = CreateWorldSpaceRay(event, view_tree);
  view_tree.HitTestFrom(event.target, world_ray, &accumulator, semantic_hit_test);
}

void InputSystem::DispatchPointerCommand(const fuchsia::ui::input::SendPointerInputCmd& command,
                                         scheduling::SessionId session_id) {
  TRACE_DURATION("input", "dispatch_command", "command", "PointerCmd");
  if (command.pointer_event.phase == fuchsia::ui::input::PointerEventPhase::HOVER) {
    FX_LOGS(WARNING) << "Injected pointer event had unexpected HOVER event.";
    return;
  }

  if (!scene_graph_) {
    FX_LOGS(INFO) << "SceneGraph wasn't set up before injecting legacy input. Dropping event.";
    return;
  }

  // Compositor and layer stack required for dispatch.
  const GlobalId compositor_id(session_id, command.compositor_id);
  gfx::CompositorWeakPtr compositor = scene_graph_->GetCompositor(compositor_id);
  if (!compositor) {
    FX_LOGS(INFO) << "Compositor wasn't set up before injecting legacy input. Dropping event.";
    return;  // It's legal to race against GFX's compositor setup.
  }

  gfx::LayerStackPtr layer_stack = compositor->layer_stack();
  if (!layer_stack) {
    FX_LOGS(INFO) << "Layer stack wasn't set up before injecting legacy input. Dropping event.";
    return;  // It's legal to race against GFX's layer stack setup.
  }

  const auto layers = layer_stack->layers();
  if (layers.empty()) {
    FX_LOGS(INFO) << "Layer wasn't set up before injecting legacy input. Dropping event.";
    return;
  }

  const gfx::ViewTree& view_tree = scene_graph_->view_tree();

  // Assume we only have one layer.
  const gfx::LayerPtr first_layer = *layers.begin();
  const std::optional<glm::mat4> world_from_screen_transform =
      first_layer->GetWorldFromScreenTransform();
  if (!world_from_screen_transform) {
    FX_LOGS(INFO) << "Wasn't able to get a WorldFromScreenTransform when injecting legacy input. "
                     "Dropping event. Is the camera or renderer uninitialized?";
    return;
  }

  const zx_koid_t scene_koid = first_layer->scene()->view_ref_koid();

  const std::optional<glm::mat4> context_from_world_transform =
      GetViewFromWorldTransform(scene_koid, view_tree);
  FX_DCHECK(context_from_world_transform);

  const uint32_t screen_width = first_layer->width();
  const uint32_t screen_height = first_layer->height();
  if (screen_width == 0 || screen_height == 0) {
    FX_LOGS(WARNING) << "Attempted to inject legacy input while Layer had 0 area";
    return;
  }
  const glm::mat4 context_from_screen_transform =
      context_from_world_transform.value() * world_from_screen_transform.value();

  InternalPointerEvent internal_event =
      GfxPointerEventToInternalEvent(command.pointer_event, scene_koid, screen_width, screen_height,
                                     context_from_screen_transform);

  switch (command.pointer_event.type) {
    case PointerEventType::TOUCH: {
      // Get stream id. Create one if this is a new stream.
      const std::pair<uint32_t, uint32_t> stream_key{internal_event.device_id,
                                                     internal_event.pointer_id};
      // ((uint64_t)internal_event.device_id << 32) | (uint64_t)internal_event.pointer_id;
      if (!gfx_legacy_streams_.count(stream_key)) {
        if (internal_event.phase != Phase::kAdd) {
          FX_LOGS(WARNING) << "Attempted to start a stream without an initial ADD.";
          return;
        }

        gfx_legacy_streams_.emplace(stream_key, NewStreamId());
      } else if (internal_event.phase == Phase::kAdd) {
        FX_LOGS(WARNING) << "Attempted to ADD twice for the same stream.";
        return;
      }
      const auto stream_id = gfx_legacy_streams_[stream_key];

      // Remove from ongoing streams on stream end.
      if (internal_event.phase == Phase::kRemove || internal_event.phase == Phase::kCancel) {
        gfx_legacy_streams_.erase(stream_key);
      }

      TRACE_DURATION("input", "dispatch_command", "command", "TouchCmd");
      TRACE_FLOW_END(
          "input", "dispatch_event_to_scenic",
          PointerTraceHACK(command.pointer_event.radius_major, command.pointer_event.radius_minor));
      InjectTouchEventHitTested(internal_event, stream_id);
      break;
    }
    case PointerEventType::MOUSE: {
      TRACE_DURATION("input", "dispatch_command", "command", "MouseCmd");
      if (internal_event.phase == Phase::kAdd || internal_event.phase == Phase::kRemove) {
        FX_LOGS(WARNING) << "Oops, mouse device (id=" << internal_event.device_id
                         << ") had an unexpected event: " << internal_event.phase;
        return;
      }
      InjectMouseEventHitTested(internal_event);
      break;
    }
    default:
      FX_LOGS(INFO) << "Stylus not supported by legacy input injection API.";
      break;
  }
}

void InputSystem::InjectTouchEventExclusive(const InternalPointerEvent& event) {
  if (!scene_graph_)
    return;

  ReportPointerEventToGfxLegacyView(
      event, event.target, fuchsia::ui::input::PointerEventType::TOUCH, scene_graph_->view_tree());
}

// The touch state machine comprises ADD/DOWN/MOVE*/UP/REMOVE. Some notes:
//  - We assume one touchscreen device, and use the device-assigned finger ID.
//  - Touch ADD associates the following ADD/DOWN/MOVE*/UP/REMOVE event sequence
//    with the set of clients available at that time. To enable gesture
//    disambiguation, we perform parallel dispatch to all clients.
//  - Touch DOWN triggers a focus change, honoring the "may receive focus" property.
//  - Touch REMOVE drops the association between event stream and client.
void InputSystem::InjectTouchEventHitTested(const InternalPointerEvent& event, StreamId stream_id) {
  FX_DCHECK(scene_graph_);

  // New stream. Collect contenders and set up a new arena.
  if (event.phase == Phase::kAdd) {
    std::vector<ContenderId> contenders = CollectContenders(stream_id, event);
    if (!contenders.empty()) {
      gesture_arenas_.emplace(stream_id, GestureArena{std::move(contenders)});
    } else if (focus_chain_root() != ZX_KOID_INVALID) {
      // No node was hit. Transfer focus to root.
      RequestFocusChange(focus_chain_root());
    }
  }

  // No arena means the contest is over and no one won.
  if (!gesture_arenas_.count(stream_id)) {
    return;
  }

  UpdateGestureContest(event, stream_id);
}

std::vector<ContenderId> InputSystem::CollectContenders(StreamId stream_id,
                                                        const InternalPointerEvent& event) {
  FX_DCHECK(event.phase == Phase::kAdd);
  FX_DCHECK(scene_graph_);

  const gfx::ViewTree& view_tree = scene_graph_->view_tree();
  std::vector<ContenderId> contenders;

  // Add an A11yLegacyContender.
  // TODO(fxbug.dev/50549): Remove when a11y is a native GD client.
  if (a11y_legacy_contender_ && IsOwnedByRootSession(view_tree, event.context)) {
    contenders.push_back(a11y_contender_id_);
  }

  {  // Add a GfxLegacyContender.
    // TODO(fxbug.dev/64206): Remove when we no longer have any legacy clients.
    gfx::TopHitAccumulator accumulator;
    HitTest(view_tree, event, accumulator, /*semantic_hit_test*/ false);
    if (accumulator.hit()) {
      const zx_koid_t hit_view_koid = accumulator.hit()->view_ref_koid;
      FX_VLOGS(1) << "View hit: [ViewRefKoid=" << hit_view_koid << "]";

      const ContenderId contender_id = AddGfxLegacyContender(stream_id, hit_view_koid);
      contenders.push_back(contender_id);
    }
  }

  return contenders;
}

void InputSystem::UpdateGestureContest(const InternalPointerEvent& event, StreamId stream_id) {
  auto arena_it = gesture_arenas_.find(stream_id);
  if (arena_it == gesture_arenas_.end())
    return;  // Contest already ended, with no winner.
  auto& arena = arena_it->second;

  const bool is_end_of_stream = event.phase == Phase::kRemove || event.phase == Phase::kCancel;
  arena.UpdateStream(/*length*/ 1, is_end_of_stream);

  // Update remaining contenders.
  // Copy the vector to avoid problems if the arena is destroyed inside of UpdateStream().
  const std::vector<ContenderId> contenders = arena.contenders();
  for (const auto contender_id : contenders) {
    auto contender_it = contenders_.find(contender_id);
    if (contender_it != contenders_.end()) {
      contender_it->second->UpdateStream(stream_id, event, is_end_of_stream);
    }
  }

  DestroyArenaIfComplete(stream_id);
}

void InputSystem::RecordGestureDisambiguationResponse(
    StreamId stream_id, ContenderId contender_id, const std::vector<GestureResponse>& responses) {
  auto arena_it = gesture_arenas_.find(stream_id);
  if (arena_it == gesture_arenas_.end() || !arena_it->second.contains(contender_id)) {
    FX_LOGS(ERROR) << "Failed to record GestureResponse";
    return;
  }
  auto& arena = arena_it->second;

  // No need to record after the contest has ended.
  if (!arena.contest_has_ended()) {
    // Update the arena.
    const ContestResults result = arena.RecordResponse(contender_id, responses);
    for (auto loser_id : result.losers) {
      auto contender = contenders_.find(loser_id);
      if (contender != contenders_.end()) {
        contenders_.at(loser_id)->EndContest(stream_id, /*awarded_win*/ false);
      }
    }
    if (result.winner) {
      auto contender_it = contenders_.find(result.winner.value());
      if (contender_it != contenders_.end()) {
        contender_it->second->EndContest(stream_id, /*awarded_win*/ true);
      }
      FX_DCHECK(arena.contenders().size() == 1u);
    }
  }

  DestroyArenaIfComplete(stream_id);
}

void InputSystem::DestroyArenaIfComplete(StreamId stream_id) {
  const auto arena_it = gesture_arenas_.find(stream_id);
  if (arena_it == gesture_arenas_.end()) {
    return;
  }

  const auto& arena = arena_it->second;

  if (arena.contenders().empty()) {
    // If no one won the contest then it will appear as if nothing was hit. Transfer focus to root.
    // TODO(fxbug.dev/59858): This probably needs to change when we figure out the exact semantics
    // we want.
    if (focus_chain_root() != ZX_KOID_INVALID) {
      RequestFocusChange(focus_chain_root());
    }
    gesture_arenas_.erase(stream_id);
  } else if (arena.contest_has_ended() && arena.stream_has_ended()) {
    // If both the contest and the stream is over, destroy the arena.
    // This branch will always be reached eventually.
    gesture_arenas_.erase(stream_id);
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
// TODO(fxbug.dev/24288): Enhance trackpad support.
void InputSystem::InjectMouseEventHitTested(const InternalPointerEvent& event) {
  FX_DCHECK(scene_graph_);
  const gfx::ViewTree& view_tree = scene_graph_->view_tree();
  const uint32_t device_id = event.device_id;
  const Phase pointer_phase = event.phase;

  if (pointer_phase == Phase::kDown) {
    // Find top-hit target and associated properties.
    gfx::TopHitAccumulator top_hit;
    HitTest(view_tree, event, top_hit, /*semantic_hit_test*/ false);

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
    ReportPointerEventToGfxLegacyView(event, top_view_koid,
                                      fuchsia::ui::input::PointerEventType::MOUSE, view_tree);
  }

  if (pointer_phase == Phase::kUp || pointer_phase == Phase::kCancel) {
    mouse_targets_.erase(device_id);
  }

  // Deal with unlatched MOVE events.
  if (pointer_phase == Phase::kChange && mouse_targets_.count(device_id) == 0) {
    // Find top-hit target and send it this move event.
    // NOTE: We may hit various mouse cursors (owned by root presenter), but |TopHitAccumulator|
    // will keep going until we find a hit with a valid owning View.
    gfx::TopHitAccumulator top_hit;
    HitTest(view_tree, event, top_hit, /*semantic_hit_test*/ false);

    if (top_hit.hit()) {
      const zx_koid_t top_view_koid = top_hit.hit()->view_ref_koid;
      ReportPointerEventToGfxLegacyView(event, top_view_koid,
                                        fuchsia::ui::input::PointerEventType::MOUSE, view_tree);
    }
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
  if (!pointer_auto_focus_) {
    return;
  }

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

bool InputSystem::IsOwnedByRootSession(const gfx::ViewTree& view_tree, zx_koid_t koid) const {
  const zx_koid_t root_koid = focus_chain_root();
  return root_koid != ZX_KOID_INVALID &&
         view_tree.SessionIdOf(koid) == view_tree.SessionIdOf(root_koid);
}

// TODO(fxbug.dev/48150): Delete when we delete the PointerCapture functionality.
void InputSystem::ReportPointerEventToPointerCaptureListener(const InternalPointerEvent& event,
                                                             const gfx::ViewTree& view_tree) const {
  if (!pointer_capture_listener_)
    return;

  const PointerCaptureListener& listener = pointer_capture_listener_.value();
  const zx_koid_t view_ref_koid = utils::ExtractKoid(listener.view_ref);
  std::optional<glm::mat4> view_from_context_transform = GetDestinationViewFromSourceViewTransform(
      /*source*/ event.context, /*destination*/ view_ref_koid, view_tree);
  if (!view_from_context_transform)
    return;

  fuchsia::ui::input::PointerEvent gfx_event = InternalPointerEventToGfxPointerEvent(
      event, view_from_context_transform.value(), fuchsia::ui::input::PointerEventType::TOUCH,
      /*trace_id*/ 0);

  ChattyCaptureLog(gfx_event);

  // TODO(fxbug.dev/42145): Implement flow control.
  listener.listener_ptr->OnPointerEvent(gfx_event, [] {});
}

void InputSystem::ReportPointerEventToGfxLegacyView(const InternalPointerEvent& event,
                                                    zx_koid_t view_ref_koid,
                                                    fuchsia::ui::input::PointerEventType type,
                                                    const gfx::ViewTree& view_tree) const {
  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "pointer");
  EventReporterWeakPtr event_reporter = view_tree.EventReporterOf(view_ref_koid);
  if (!event_reporter)
    return;

  std::optional<glm::mat4> view_from_context_transform = GetDestinationViewFromSourceViewTransform(
      /*source*/ event.context, /*destination*/ view_ref_koid, view_tree);
  if (!view_from_context_transform)
    return;

  const uint64_t trace_id = NextTraceId();
  TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", trace_id);
  InputEvent input_event;
  input_event.set_pointer(InternalPointerEventToGfxPointerEvent(
      event, view_from_context_transform.value(), type, trace_id));
  FX_VLOGS(1) << "Event dispatch to view=" << view_ref_koid << ": " << input_event;
  ChattyGfxLog(input_event);
  event_reporter->EnqueueEvent(std::move(input_event));
}

}  // namespace input
}  // namespace scenic_impl
