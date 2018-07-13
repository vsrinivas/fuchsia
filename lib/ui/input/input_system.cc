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
#include "garnet/lib/ui/input/focus.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "lib/escher/geometry/types.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"
#include "lib/ui/geometry/cpp/formatting.h"
#include "lib/ui/input/cpp/formatting.h"

namespace scenic {
namespace input {

InputSystem::InputSystem(SystemContext context, gfx::GfxSystem* gfx_system)
    : System(std::move(context)), gfx_system_(gfx_system) {
  FXL_CHECK(gfx_system_);
  FXL_LOG(INFO) << "Scenic input system started.";
}

std::unique_ptr<CommandDispatcher> InputSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return std::make_unique<InputCommandDispatcher>(std::move(context),
                                                  gfx_system_);
}

InputCommandDispatcher::InputCommandDispatcher(
    CommandDispatcherContext context, scenic::gfx::GfxSystem* gfx_system)
    : CommandDispatcher(std::move(context)),
      gfx_system_(gfx_system),
      focus_(std::make_unique<Focus>()) {
  FXL_DCHECK(gfx_system_);
}

InputCommandDispatcher::~InputCommandDispatcher() = default;

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
  FXL_VLOG(2) << "Scenic input - command received: " << input_command;

  if (input_command.is_send_pointer_input()) {
    const fuchsia::ui::input::SendPointerInputCmd& cmd =
        input_command.send_pointer_input();

    if (cmd.pointer_event.phase == PointerEventPhase::DOWN) {
      escher::ray4 ray;
      {
        fuchsia::math::PointF point;
        point.x = cmd.pointer_event.x;
        point.y = cmd.pointer_event.y;
        FXL_VLOG(1) << "HitTest: point " << point;

        // Start just above the (x,y) point in the device's coordinate space.
        ray.origin = escher::vec4(point.x, point.y, -1.f, 1.f);
        // Point down into the scene.
        ray.direction = escher::vec4(0.f, 0.f, 1.f, 0.f);
      }

      std::vector<gfx::Hit> hits;
      {
        FXL_DCHECK(cmd.compositor_id != 0)
            << "Pointer event without compositor_id.";
        gfx::Compositor* compositor =
            gfx_system_->GetCompositor(cmd.compositor_id);
        FXL_DCHECK(compositor != nullptr) << "Compositor not found.";

        gfx::LayerStackPtr layer_stack = compositor->layer_stack();
        std::unique_ptr<gfx::HitTester> hit_tester =
            std::make_unique<gfx::GlobalHitTester>();
        hits = layer_stack->HitTest(ray, hit_tester.get());
      }

      FXL_VLOG(1) << "Hits acquired, count: " << hits.size();

      // Construct focus chain.
      std::unique_ptr<Focus> new_focus = std::make_unique<Focus>();
      for (gfx::Hit hit : hits) {
        ViewId view_id;
        view_id.session_id = hit.view_session;
        view_id.resource_id = hit.view_resource;
        gfx::ViewPtr owning_view = FindView(view_id);
        if (owning_view) {
          new_focus->chain.push_back(view_id);
        }
      }

      bool switch_focus =
          focus_->chain.size() == 0 ||     // old focus empty, or
          new_focus->chain.size() == 0 ||  // new focus empty, or
          focus_->chain.front() != new_focus->chain.front();  // focus changed
      if (switch_focus) {
        const int64_t focus_time = NowInNs();
        if (focus_ && !focus_->chain.empty()) {
          FXL_VLOG(1) << "Input focus lost by " << focus_->chain.front();
          gfx::ViewPtr view = FindView(focus_->chain.front());
          if (view) {
            fuchsia::ui::input::FocusEvent focus;
            focus.event_time = focus_time;
            focus.focused = false;
            EnqueueEvent(view, focus);
          }
        }
        if (!new_focus->chain.empty()) {
          FXL_VLOG(1) << "Input focus gained by " << new_focus->chain.front();
          gfx::ViewPtr view = FindView(new_focus->chain.front());
          if (view) {
            fuchsia::ui::input::FocusEvent focus;
            focus.event_time = focus_time;
            focus.focused = true;
            EnqueueEvent(view, focus);
          }
        }
        focus_ = std::move(new_focus);
      }
    }
  } else if (input_command.is_send_keyboard_input()) {
    FXL_VLOG(1) << "Scenic dispatch: " << input_command.send_keyboard_input();
  }
}

gfx::ViewPtr InputCommandDispatcher::FindView(ViewId view_id) {
  if (view_id.session_id == 0u && view_id.resource_id == 0u) {
    return nullptr;  // Don't bother with empty tokens.
  }

  gfx::Session* session = gfx_system_->GetSession(view_id.session_id);
  if (session && session->is_valid()) {
    return session->resources()->FindResource<gfx::View>(view_id.resource_id);
  }

  return nullptr;
}

void InputCommandDispatcher::EnqueueEvent(
    gfx::ViewPtr view, fuchsia::ui::input::FocusEvent focus) {
  FXL_DCHECK(view);
  gfx::Session* session = view->session();
  EventReporter* event_reporter = session->event_reporter();
  if (session && session->is_valid() && event_reporter) {
    fuchsia::ui::input::InputEvent input_event;
    input_event.set_focus(std::move(focus));
    fuchsia::ui::scenic::Event event;
    event.set_input(std::move(input_event));
    event_reporter->EnqueueEvent(std::move(event));
    return;
  }

  if (!(session && session->is_valid())) {
    FXL_DLOG(INFO) << "Scenic input dispatch: invalid session.";
  }
  if (!event_reporter) {
    FXL_DLOG(INFO) << "Scnenic input dispatch: no event reporter";
  }
}

}  // namespace input
}  // namespace scenic
