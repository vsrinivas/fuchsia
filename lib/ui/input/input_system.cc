// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/input_system.h"

#include <memory>

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "garnet/lib/ui/gfx/engine/hit.h"
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "garnet/lib/ui/input/focus.h"
#include "lib/escher/geometry/types.h"
#include "lib/fxl/logging.h"
#include "lib/ui/geometry/cpp/formatting.h"
#include "lib/ui/input/cpp/formatting.h"

namespace scenic {
namespace input {

InputSystem::InputSystem(SystemContext context, gfx::GfxSystem* gfx_system)
    : System(std::move(context)), gfx_system_(gfx_system) {
  FXL_CHECK(gfx_system_);
  FXL_LOG(INFO) << "Scenic input system started.";
}

InputSystem::~InputSystem() = default;

std::unique_ptr<CommandDispatcher> InputSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return std::make_unique<InputCommandDispatcher>(std::move(context),
                                                  gfx_system_);
}

InputCommandDispatcher::InputCommandDispatcher(
    CommandDispatcherContext context, scenic::gfx::GfxSystem* gfx_system)
    : CommandDispatcher(std::move(context)), gfx_system_(gfx_system) {
  FXL_DCHECK(gfx_system_);
}

InputCommandDispatcher::~InputCommandDispatcher() = default;

void InputCommandDispatcher::DispatchCommand(
    fuchsia::ui::scenic::Command command) {
  using ScenicCommand = fuchsia::ui::scenic::Command;
  using InputCommand = fuchsia::ui::input::Command;
  using fuchsia::ui::input::PointerEvent;
  using fuchsia::ui::input::PointerEventPhase;
  using fuchsia::ui::input::KeyboardEvent;

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

      // Set up focus chain, send out focus events
      FocusChain focus;
      for (gfx::Hit hit : hits) {
        ViewId view_id;

        gfx::Session* session = gfx_system_->GetSession(hit.view_session);
        if (session) {
          gfx::ViewPtr owning_view =
              session->resources()->FindResource<gfx::View>(hit.view_resource);
          if (owning_view) {
            view_id.session_id = hit.view_session;
            view_id.resource_id = hit.view_resource;
            focus.chain.push_back(view_id);
          }
        }
      }

    }
  } else if (input_command.is_send_keyboard_input()) {
    FXL_VLOG(1) << "Scenic dispatch: " << input_command.send_keyboard_input();
  }
}

}  // namespace input
}  // namespace scenic
