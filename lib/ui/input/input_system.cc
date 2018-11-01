// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/input_system.h"

#include <memory>

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

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
#include "lib/component/cpp/startup_context.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/util/type_utils.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"
#include "lib/ui/geometry/cpp/formatting.h"
#include "lib/ui/input/cpp/formatting.h"

namespace scenic_impl {
namespace input {

using ScenicCommand = fuchsia::ui::scenic::Command;
using InputCommand = fuchsia::ui::input::Command;
using fuchsia::ui::input::FocusEvent;
using fuchsia::ui::input::ImeService;
using fuchsia::ui::input::ImeServicePtr;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::KeyboardEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::SendKeyboardInputCmd;
using fuchsia::ui::input::SendPointerInputCmd;
using fuchsia::ui::input::SetHardKeyboardDeliveryCmd;
using fuchsia::ui::input::SetParallelDispatchCmd;

namespace {
// Helper for DispatchCommand.
int64_t NowInNs() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

// Helper for DispatchCommand.
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
  return {{x, y, -1.f, 1.f},  // Origin as homogeneous point.
          {0.f, 0.f, 1.f, 0.f}};
}

// Helper for DispatchCommand.
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

// Helper for DispatchCommand.
glm::mat4 FindGlobalTransform(gfx::ResourcePtr view) {
  glm::mat4 global_transform(1.f);  // Default is identity transform.
  if (!view) {
    return global_transform;
  }

  if (view->IsKindOf<gfx::View>()) {
    gfx::ViewPtr v2view = view->As<gfx::View>();
    if (v2view->view_holder() && v2view->view_holder()->parent()) {
      global_transform = v2view->view_holder()->parent()->GetGlobalTransform();
    }
  } else {
    // TODO(SCN-1006): After v2 transition, remove this clause.
    FXL_DCHECK(view->IsKindOf<gfx::Import>());
    if (gfx::ImportPtr import = view->As<gfx::Import>()) {
      if (gfx::Resource* delegate = import->delegate()) {
        FXL_DCHECK(delegate->IsKindOf<gfx::Node>());
        if (gfx::NodePtr node = delegate->As<gfx::Node>()) {
          global_transform = node->GetGlobalTransform();
        }
      }
    }
  }
  return global_transform;
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

// Helper for DispatchCommand.
// Ensure sessions get each event just once: stamp out duplicate
// sessions in the rest of the hits. This assumes:
// - there may be a ViewManager that interposes many Import nodes
// - each client has at most one "view" (whether View or Import)
// - each client receives at most one hit per view
// which reflects the pre-v2 migration scene graph.
// TODO(SCN-1006): Enable multiple Views per client.
// TODO(SCN-935): Return full set of hits to each client.
void RemoveHitsFromSameSession(SessionId session_id, size_t start_idx,
                               std::vector<gfx::ResourcePtr>* views) {
  FXL_DCHECK(views);
  for (size_t k = start_idx; k < views->size(); ++k) {
    if ((*views)[k] && ((*views)[k]->session()->id() == session_id)) {
      (*views)[k] = nullptr;
    }
  }
}

// Helper for DispatchCommand.
bool IsFocusChange(gfx::ResourcePtr view) {
  FXL_DCHECK(view);
  if (view->IsKindOf<gfx::View>()) {
    gfx::ViewPtr view_ptr = view->As<gfx::View>();
    if (view_ptr->connected()) {
      return view_ptr->view_holder()->view_properties().focus_change;
    }
  } else {
    // TODO(SCN-1026): Convert to query v2 view properties.
    // TODO(SCN-1006): After v2 transition, remove this clause.
    // We traverse up the scene graph to find the closest "ancestor" Import
    // starting from a given Import. We assume the scene graph is set up by
    // ViewManager in a very particular configuration for this traversal.
    FXL_DCHECK(view->IsKindOf<gfx::Import>());
    if (gfx::ImportPtr import = view->As<gfx::Import>()) {
      if (gfx::Resource* imported = import->imported_resource()) {
        if (imported->IsKindOf<gfx::EntityNode>()) {
          gfx::NodePtr attach_point = imported->As<gfx::EntityNode>();
          if (gfx::Node* delegate = attach_point->parent()) {
            if (gfx::Node* parent_node = delegate->parent()) {
              if (parent_node->imports().size() > 0) {
                return parent_node->imports()[0]->focusable();
              }
            }
          }
        }
      }
    }
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

    text_sync_service_ = this->context()
                             ->app_context()
                             ->ConnectToEnvironmentService<ImeService>();
    text_sync_service_.set_error_handler([](zx_status_t status) {
      FXL_LOG(ERROR) << "Scenic lost connection to TextSync";
    });

    FXL_LOG(INFO) << "Scenic input system initialized.";
  });
}

std::unique_ptr<CommandDispatcher> InputSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return std::make_unique<InputCommandDispatcher>(std::move(context),
                                                  gfx_system_, this);
}

InputCommandDispatcher::InputCommandDispatcher(CommandDispatcherContext context,
                                               gfx::GfxSystem* gfx_system,
                                               InputSystem* input_system)
    : CommandDispatcher(std::move(context)),
      gfx_system_(gfx_system),
      input_system_(input_system) {
  FXL_CHECK(gfx_system_);
  FXL_CHECK(input_system_);
}

void InputCommandDispatcher::DispatchCommand(ScenicCommand command) {
  FXL_DCHECK(command.Which() == ScenicCommand::Tag::kInput);

  InputCommand& input = command.input();
  if (input.is_send_keyboard_input()) {
    DispatchCommand(std::move(input.send_keyboard_input()));
  } else if (input.is_send_pointer_input()) {
    DispatchCommand(std::move(input.send_pointer_input()));
  } else if (input.is_set_hard_keyboard_delivery()) {
    DispatchCommand(std::move(input.set_hard_keyboard_delivery()));
  } else if (input.is_set_parallel_dispatch()) {
    DispatchCommand(std::move(input.set_parallel_dispatch()));
  }
}

void InputCommandDispatcher::DispatchCommand(
    const SendPointerInputCmd command) {
  const uint32_t pointer_id = command.pointer_event.pointer_id;
  const PointerEventPhase pointer_phase = command.pointer_event.phase;

  // We perform a hit test on the ADD phase so that it's clear which targets
  // should continue to receive events from that particular pointer. The focus
  // events are delivered on the subsequent DOWN phase. This makes sense for
  // touch pointers, where the touchscreen's DeviceState ensures that ADD and
  // DOWN are coincident in time and space. This scheme won't necessarily work
  // for a stylus pointer, which may HOVER between ADD and DOWN.
  // TODO(SCN-940, SCN-164): Implement stylus support.
  if (pointer_phase == PointerEventPhase::ADD) {
    escher::ray4 ray = CreateScreenPerpendicularRay(command.pointer_event.x,
                                                    command.pointer_event.y);
    FXL_VLOG(1) << "HitTest: device point (" << ray.origin.x << ", "
                << ray.origin.y << ")";

    std::vector<gfx::Hit> hits;
    {
      gfx::Compositor* compositor =
          gfx_system_->GetCompositor(command.compositor_id);
      if (!compositor)
        return;  // Race with GFX; no delivery.

      gfx::LayerStackPtr layer_stack = compositor->layer_stack();
      if (!layer_stack)
        return;  // Race with GFX; no delivery.

      auto hit_tester = std::make_unique<gfx::GlobalHitTester>();
      hits = layer_stack->HitTest(ray, hit_tester.get());
    }
    FXL_VLOG(1) << "Hits acquired, count: " << hits.size();

    // Find input targets.  Honor the "input masking" view property.
    ViewStack hit_views;
    {
      // Precompute the View for each hit. Don't hold on to these RefPtrs!
      std::vector<gfx::ResourcePtr> views;
      views.reserve(hits.size());
      for (const gfx::Hit& hit : hits) {
        FXL_DCHECK(hit.node);  // Raw ptr, use it and let go.
        views.push_back(hit.node->FindOwningView());
      }
      FXL_DCHECK(hits.size() == views.size());

      // Find the global transform for each hit, fill out hit_views.
      for (size_t i = 0; i < hits.size(); ++i) {
        if (gfx::ResourcePtr view = views[i]) {
          glm::mat4 global_transform = FindGlobalTransform(view);
          hit_views.stack.push_back(
              {{view->session()->id(), view->id()}, global_transform});
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

    // Save targets for consistent delivery of pointer events.
    pointer_targets_[pointer_id] = hit_views;

  } else if (pointer_phase == PointerEventPhase::DOWN) {
    // New focus can be: (1) empty (if no views), or (2) the old focus (either
    // deliberately, or by the no-focus property), or (3) another view.
    GlobalId new_focus;
    if (!pointer_targets_[pointer_id].stack.empty()) {
      if (pointer_targets_[pointer_id].focus_change) {
        new_focus = pointer_targets_[pointer_id].stack[0].view_id;
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
  for (const auto& entry : pointer_targets_[pointer_id].stack) {
    escher::ray4 screen_ray = CreateScreenPerpendicularRay(
        command.pointer_event.x, command.pointer_event.y);
    escher::vec2 hit =
        TransformPointerEvent(screen_ray, entry.global_transform);

    auto clone = ClonePointerWithCoords(command.pointer_event, hit.x, hit.y);
    EnqueueEventToView(entry.view_id, std::move(clone));

    if (!parallel_dispatch_) {
      break;  // TODO(SCN-1047): Remove when gesture disambiguation is ready.
    }
  }

  if (pointer_phase == PointerEventPhase::REMOVE ||
      pointer_phase == PointerEventPhase::CANCEL) {
    pointer_targets_.erase(pointer_id);
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
  const SessionId session_id = context()->session_id();
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
  if (gfx::Session* session = gfx_system_->GetSession(view_id.session_id)) {
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
