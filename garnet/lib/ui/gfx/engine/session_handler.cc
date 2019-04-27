// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session_handler.h"

#include <memory>

#include "garnet/lib/ui/scenic/session.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace scenic_impl {
namespace gfx {

SessionHandler::SessionHandler(CommandDispatcherContext dispatcher_context,
                               SessionContext session_context,
                               EventReporter* event_reporter,
                               ErrorReporter* error_reporter,
                               inspect::Object inspect_object)
    : TempSessionDelegate(std::move(dispatcher_context)),

      session_(std::make_unique<Session>(
          command_dispatcher_context()->session_id(),
          std::move(session_context), event_reporter, error_reporter,
          std::move(inspect_object))) {}

void SessionHandler::Present(
    uint64_t presentation_time, std::vector<zx::event> acquire_fences,
    std::vector<zx::event> release_fences,
    fuchsia::ui::scenic::Session::PresentCallback callback) {
  if (!session_->ScheduleUpdate(
          presentation_time, std::move(buffered_commands_),
          std::move(acquire_fences), std::move(release_fences),
          std::move(callback))) {
    KillSession();
  } else {
    buffered_commands_.clear();
  }
}

void SessionHandler::DispatchCommand(fuchsia::ui::scenic::Command command) {
  FXL_DCHECK(command.Which() == fuchsia::ui::scenic::Command::Tag::kGfx);
  buffered_commands_.emplace_back(std::move(command.gfx()));
}

void SessionHandler::KillSession() {
  // Since this is essentially a self destruct
  // call, it's safest not call anything after this
  command_dispatcher_context()->KillSession();
}

}  // namespace gfx
}  // namespace scenic_impl
