// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session_handler.h"

#include "garnet/lib/ui/scenic/session.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace scenic {
namespace gfx {

SessionHandler::SessionHandler(CommandDispatcherContext dispatcher_context,
                               Engine* engine,
                               SessionId session_id,
                               EventReporter* event_reporter,
                               ErrorReporter* error_reporter)
    : TempSessionDelegate(std::move(dispatcher_context)),
      session_manager_(engine->session_manager()),
      event_reporter_(event_reporter),
      error_reporter_(error_reporter),
      session_(::fxl::MakeRefCounted<scenic::gfx::Session>(
          session_id,
          engine,
          static_cast<EventReporter*>(this),
          error_reporter)) {
  FXL_DCHECK(engine);
}

SessionHandler::~SessionHandler() {
  TearDown();
}

void SessionHandler::SendEvents(::f1dl::VectorPtr<ui::EventPtr> events) {
  event_reporter_->SendEvents(std::move(events));
}

void SessionHandler::Enqueue(::f1dl::VectorPtr<ui::CommandPtr> commands) {
  // TODO: Add them all at once instead of iterating.  The problem
  // is that ::fidl::Array doesn't support this.  Or, at least reserve
  // enough space.  But ::fidl::Array doesn't support this, either.
  for (auto& command : *commands) {
    FXL_CHECK(command->which() == ui::Command::Tag::GFX);
    buffered_commands_.push_back(std::move(command->get_gfx()));
  }
}

void SessionHandler::Present(uint64_t presentation_time,
                             ::f1dl::VectorPtr<zx::event> acquire_fences,
                             ::f1dl::VectorPtr<zx::event> release_fences,
                             const ui::Session::PresentCallback& callback) {
  if (!session_->ScheduleUpdate(
          presentation_time, std::move(buffered_commands_),
          std::move(acquire_fences), std::move(release_fences), callback)) {
    BeginTearDown();
  }
}

void SessionHandler::HitTest(uint32_t node_id,
                             ui::gfx::vec3Ptr ray_origin,
                             ui::gfx::vec3Ptr ray_direction,
                             const ui::Session::HitTestCallback& callback) {
  session_->HitTest(node_id, std::move(ray_origin), std::move(ray_direction),
                    callback);
}

void SessionHandler::HitTestDeviceRay(
    ui::gfx::vec3Ptr ray_origin,
    ui::gfx::vec3Ptr ray_direction,
    const ui::Session::HitTestDeviceRayCallback& callback) {
  session_->HitTestDeviceRay(std::move(ray_origin), std::move(ray_direction),
                             callback);
}

bool SessionHandler::ApplyCommand(const ui::CommandPtr& command) {
  // TODO(MZ-469): Implement once we push session management into Mozart.
  FXL_CHECK(false);
  return false;
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
