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
#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "lib/escher/geometry/types.h"
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

void InputCommandDispatcher::DispatchCommand(
    fuchsia::ui::scenic::Command command) {
  using ScenicCommand = fuchsia::ui::scenic::Command;
  using InputCommand = fuchsia::ui::input::Command;
  using fuchsia::ui::input::KeyboardEvent;
  using fuchsia::ui::input::PointerEvent;
  using fuchsia::ui::input::PointerEventPhase;

  FXL_DCHECK(command.Which() == ScenicCommand::Tag::kInput);

  const InputCommand& input_command = command.input();
  if (input_command.is_send_pointer_input()) {
    const fuchsia::ui::input::SendPointerInputCmd& send =
        input_command.send_pointer_input();
    const uint32_t pointer_id = send.pointer_event.pointer_id;
    const PointerEventPhase pointer_phase = send.pointer_event.phase;

    // TODO(SCN-164): ADD events may require additional REMOVE/CANCEL matching.
    if (pointer_phase == PointerEventPhase::DOWN) {
      escher::ray4 ray;
      {
        fuchsia::math::PointF point;
        point.x = send.pointer_event.x;
        point.y = send.pointer_event.y;
        FXL_VLOG(1) << "HitTest: point " << point;

        // Start just above the (x,y) point in the device's coordinate space.
        ray.origin = escher::vec4(point.x, point.y, -1.f, 1.f);
        // Point down into the scene.
        ray.direction = escher::vec4(0.f, 0.f, 1.f, 0.f);
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
      }
      FXL_VLOG(1) << "Hits acquired, count: " << hits.size();

      // Find input targets.  Honor the "input masking" view property.
      ViewStack hit_views;
      for (size_t i = 0; i < hits.size(); ++i) {
        ViewId view_id(hits[i].view_session, hits[i].view_resource);
        if (gfx::ViewPtr view = FindView(view_id)) {
          hit_views.stack.push_back(view_id);
          if (/*TODO(SCN-919): view_id may mask input */ false) {
            break;
          }
          // Ensure Views are unique: stamp out duplicates in the hits vector.
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

      // New focus can be: (1) empty (if no views), or (2) the old focus (either
      // deliberately, or by the no-focus property), or (3) another view.
      ViewId new_focus;
      if (!hit_views.stack.empty()) {
        // TODO(SCN-919): Honor the "focus_change" view property.
        if (/*top view is focusable*/ true) {
          new_focus = hit_views.stack[0];
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

      // Enable consistent delivery of pointer events.
      pointer_targets_[pointer_id] = hit_views;
    }  // Pointer DOWN event

    // Input delivery must be parallel; needed for gesture disambiguation.
    for (ViewId& id : pointer_targets_[pointer_id].stack) {
      if (gfx::ViewPtr view = FindView(id)) {
        EnqueueEventToView(view, send.pointer_event);
      }
    }

    if (pointer_phase == PointerEventPhase::UP ||
        pointer_phase == PointerEventPhase::CANCEL) {
      pointer_targets_.erase(pointer_id);
    }
  } else if (input_command.is_send_keyboard_input()) {
    // Send keyboard events to active focus.
    if (gfx::ViewPtr view = FindView(focus_)) {
      EnqueueEventToView(view,
                         input_command.send_keyboard_input().keyboard_event);
    }
  }
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
