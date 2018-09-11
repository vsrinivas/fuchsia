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
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"
#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/util/type_utils.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"
#include "lib/ui/geometry/cpp/formatting.h"
#include "lib/ui/input/cpp/formatting.h"

namespace scenic_impl {
namespace input {

InputSystem::InputSystem(SystemContext context, gfx::GfxSystem* gfx_system)
    : System(std::move(context), /*initialized_after_construction*/ false),
      gfx_system_(gfx_system) {
  FXL_CHECK(gfx_system_);
  gfx_system_->AddInitClosure([this]() {
    SetToInitialized();
    FXL_LOG(INFO) << "Scenic input system initialized.";
  });
}

std::unique_ptr<CommandDispatcher> InputSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return std::make_unique<InputCommandDispatcher>(std::move(context),
                                                  gfx_system_);
}

InputCommandDispatcher::InputCommandDispatcher(
    CommandDispatcherContext context, gfx::GfxSystem* gfx_system)
    : CommandDispatcher(std::move(context)),
      gfx_system_(gfx_system) {
  FXL_CHECK(gfx_system_);
}

// Helper for DispatchCommand.
static int64_t NowInNs() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

// Helper for DispatchCommand.
static escher::ray4 CreateScreenPerpendicularRay(float x, float y) {
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
static escher::vec2 TransformPointerEvent(
    const escher::ray4& ray, const escher::mat4& transform) {
  escher::ray4 local_ray = glm::inverse(transform) * ray;

  // We treat distance as 0 to simplify; otherwise the formula is:
  // hit = homogenize(local_ray.origin + distance * local_ray.direction);
  escher::vec2 hit(escher::homogenize(local_ray.origin));

  FXL_VLOG(2) << "Coordinate transform (device->view): (" << ray.origin.x
              << ", " << ray.origin.x << ")->(" << hit.x << ", " << hit.y
              << ")";
  return hit;
}

void InputCommandDispatcher::DispatchCommand(
    fuchsia::ui::scenic::Command command) {
  using ScenicCommand = fuchsia::ui::scenic::Command;
  using InputCommand = fuchsia::ui::input::Command;
  using fuchsia::ui::input::KeyboardEvent;
  using fuchsia::ui::input::PointerEvent;
  using fuchsia::ui::input::PointerEventPhase;

  FXL_DCHECK(command.Which() == ScenicCommand::Tag::kInput);

  const InputCommand& input_command = command.input();
  if (input_command.is_send_keyboard_input()) {
    // Send keyboard events to active focus.
    if (gfx::ViewPtr view = FindView(focus_)) {
      EnqueueEventToView(view,
                         input_command.send_keyboard_input().keyboard_event);
    }
  } else if (input_command.is_send_pointer_input()) {
    const fuchsia::ui::input::SendPointerInputCmd& send =
        input_command.send_pointer_input();
    const uint32_t pointer_id = send.pointer_event.pointer_id;
    const PointerEventPhase pointer_phase = send.pointer_event.phase;

    // We perform a hit test on the ADD phase so that it's clear which targets
    // should continue to receive events from that particular pointer. The focus
    // events are delivered on the subsequent DOWN phase. This makes sense for
    // touch pointers, where the touchscreen's DeviceState ensures that ADD and
    // DOWN are coincident in time and space. This scheme won't necessarily work
    // for a stylus pointer, which may HOVER between ADD and DOWN.
    // TODO(SCN-940, SCN-164): Implement stylus support.
    if (pointer_phase == PointerEventPhase::ADD) {
      escher::ray4 ray;
      {
        fuchsia::math::PointF point;
        point.x = send.pointer_event.x;
        point.y = send.pointer_event.y;
        ray = CreateScreenPerpendicularRay(point.x, point.y);
        FXL_VLOG(1) << "HitTest: device point " << point;
      }

      std::vector<gfx::Hit> hits;
      {
        gfx::Compositor* compositor =
            gfx_system_->GetCompositor(send.compositor_id);
        FXL_DCHECK(compositor)
            << "No compositor found (id=" << send.compositor_id << ").";

        gfx::LayerStackPtr layer_stack = compositor->layer_stack();
        auto hit_tester = std::make_unique<gfx::GlobalHitTester>();
        hits = layer_stack->HitTest(ray, hit_tester.get());
        FXL_VLOG(1) << "Hits acquired, count: " << hits.size();
      }

      // Find input targets.  Honor the "input masking" view property.
      ViewStack hit_views;
      for (size_t i = 0; i < hits.size(); ++i) {
        ViewId view_id(hits[i].view_session, hits[i].view_resource);
        if (gfx::ViewPtr view = FindView(view_id)) {
          glm::mat4 global_transform(1.f);  // Identity transform as default.
          if (view->view_holder() && view->view_holder()->parent()) {
            global_transform =
                view->view_holder()->parent()->GetGlobalTransform();
          }
          hit_views.stack.push_back({view_id, global_transform});
          if (/*TODO(SCN-919): view_id may mask input */ false) {
            break;
          }
          // Ensure Views are unique: stamp out duplicates in the hits vector.
          // TODO(SCN-935): Return hits (in model space coordinates) to clients.
          for (size_t k = i + 1; k < hits.size(); ++k) {
            ViewId next(hits[k].view_session, hits[k].view_resource);
            if (view_id == next) {
              hits[k].view_session = 0u;
              hits[k].view_resource = 0u;
            }
          }
        }
      }
      FXL_VLOG(1) << "View stack of hits: " << hit_views;

      // Save targets for consistent delivery of pointer events.
      pointer_targets_[pointer_id] = hit_views;

    } else if (pointer_phase == PointerEventPhase::DOWN) {
      // New focus can be: (1) empty (if no views), or (2) the old focus (either
      // deliberately, or by the no-focus property), or (3) another view.
      ViewId new_focus;
      if (!pointer_targets_[pointer_id].stack.empty()) {
        // TODO(SCN-919): Honor the "focus_change" view property.
        if (/*top view is focusable*/ true) {
          new_focus = pointer_targets_[pointer_id].stack[0].id;
        } else {
          new_focus = focus_;  // No focus change.
        }
      }
      FXL_VLOG(1) << "Focus, old and new: " << focus_ << " vs " << new_focus;

      // Deliver focus events.
      if (focus_ != new_focus) {
        const int64_t focus_time = NowInNs();
        if (focus_) {
          if (gfx::ViewPtr view = FindView(focus_)) {
            fuchsia::ui::input::FocusEvent event;
            event.event_time = focus_time;
            event.focused = false;
            EnqueueEventToView(view, event);
            FXL_VLOG(1) << "Input focus lost by " << focus_;
          }
        }
        if (new_focus) {
          if (gfx::ViewPtr view = FindView(new_focus)) {
            fuchsia::ui::input::FocusEvent event;
            event.event_time = focus_time;
            event.focused = true;
            EnqueueEventToView(view, event);
            FXL_VLOG(1) << "Input focus gained by " << new_focus;
          }
        }
        focus_ = new_focus;
      }
    }

    // Input delivery must be parallel; needed for gesture disambiguation.
    for (const auto& entry : pointer_targets_[pointer_id].stack) {
      if (gfx::ViewPtr view = FindView(entry.id)) {
        escher::ray4 screen_ray = CreateScreenPerpendicularRay(
            send.pointer_event.x, send.pointer_event.y);
        escher::vec2 hit =
            TransformPointerEvent(screen_ray, entry.global_transform);

        fuchsia::ui::input::PointerEvent clone;
        fidl::Clone(command.input().send_pointer_input().pointer_event, &clone);
        clone.x = hit.x;
        clone.y = hit.y;

        EnqueueEventToView(view, std::move(clone));
      }
    }

    if (pointer_phase == PointerEventPhase::REMOVE ||
        pointer_phase == PointerEventPhase::CANCEL) {
      pointer_targets_.erase(pointer_id);
    }
  }  // send pointer event
}

gfx::ViewPtr InputCommandDispatcher::FindView(ViewId view_id) {
  if (!view_id) {
    return nullptr;  // Don't bother.
  }

  gfx::Session* session = gfx_system_->GetSession(view_id.session_id);
  if (session && session->is_valid()) {
    return session->resources()->FindResource<gfx::View>(view_id.resource_id);
  }

  return nullptr;
}

void InputCommandDispatcher::EnqueueEventToView(
    gfx::ViewPtr view, fuchsia::ui::input::FocusEvent focus) {
  FXL_DCHECK(view);

  fuchsia::ui::input::InputEvent event;
  event.set_focus(std::move(focus));

  view->session()->EnqueueEvent(std::move(event));
}

void InputCommandDispatcher::EnqueueEventToView(
    gfx::ViewPtr view, fuchsia::ui::input::KeyboardEvent keyboard) {
  FXL_DCHECK(view);

  fuchsia::ui::input::InputEvent event;
  event.set_keyboard(std::move(keyboard));

  view->session()->EnqueueEvent(std::move(event));
}

void InputCommandDispatcher::EnqueueEventToView(
    gfx::ViewPtr view, fuchsia::ui::input::PointerEvent pointer) {
  FXL_DCHECK(view);

  fuchsia::ui::input::InputEvent event;
  event.set_pointer(std::move(pointer));

  view->session()->EnqueueEvent(std::move(event));
}

}  // namespace input
}  // namespace scenic_impl
