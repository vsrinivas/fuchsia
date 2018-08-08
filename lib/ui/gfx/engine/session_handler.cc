// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session_handler.h"

#include "garnet/lib/ui/scenic/session.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace scenic {
namespace gfx {

SessionHandler::SessionHandler(CommandDispatcherContext dispatcher_context,
                               Engine* engine, SessionId session_id,
                               EventReporter* event_reporter,
                               ErrorReporter* error_reporter)
    : TempSessionDelegate(std::move(dispatcher_context)),
      session_manager_(engine->session_manager()),
      event_reporter_(event_reporter),
      error_reporter_(error_reporter),
      session_(::fxl::MakeRefCounted<scenic::gfx::Session>(
          session_id, engine, event_reporter, error_reporter)) {
  FXL_DCHECK(engine);
}

SessionHandler::~SessionHandler() { TearDown(); }

void SessionHandler::Present(
    uint64_t presentation_time, ::fidl::VectorPtr<zx::event> acquire_fences,
    ::fidl::VectorPtr<zx::event> release_fences,
    fuchsia::ui::scenic::Session::PresentCallback callback) {
  if (!session_->ScheduleUpdate(
          presentation_time, std::move(buffered_commands_),
          std::move(acquire_fences), std::move(release_fences),
          std::move(callback))) {
    BeginTearDown();
  }
  buffered_commands_.clear();
}

void SessionHandler::HitTest(
    uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
    ::fuchsia::ui::gfx::vec3 ray_direction,
    fuchsia::ui::scenic::Session::HitTestCallback callback) {
  session_->HitTest(node_id, std::move(ray_origin), std::move(ray_direction),
                    std::move(callback));
}

void SessionHandler::HitTestDeviceRay(
    ::fuchsia::ui::gfx::vec3 ray_origin, ::fuchsia::ui::gfx::vec3 ray_direction,
    fuchsia::ui::scenic::Session::HitTestDeviceRayCallback callback) {
  session_->HitTestDeviceRay(std::move(ray_origin), std::move(ray_direction),
                             std::move(callback));
}

void SessionHandler::DispatchCommand(fuchsia::ui::scenic::Command command) {
  FXL_DCHECK(command.Which() == fuchsia::ui::scenic::Command::Tag::kGfx);
  buffered_commands_.emplace_back(std::move(command.gfx()));
}

void SessionHandler::BeginTearDown() {
  session_manager_->TearDownSession(session_->id());
  FXL_DCHECK(!session_->is_valid());
}

void SessionHandler::TearDown() {
  session_->TearDown();

  // Close the parent Mozart session.
  if (context() && context()->session()) {
    context()->scenic()->CloseSession(context()->session());
  }
}

}  // namespace gfx
}  // namespace scenic
