// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/session_handler.h"

#include <memory>

#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/scenic/lib/scenic/session.h"

namespace scenic_impl {
namespace gfx {

SessionHandler::SessionHandler(CommandDispatcherContext dispatcher_context,
                               SessionContext session_context,
                               std::shared_ptr<EventReporter> event_reporter,
                               std::shared_ptr<ErrorReporter> error_reporter,
                               inspect_deprecated::Node inspect_object)
    : TempSessionDelegate(std::move(dispatcher_context)),

      session_(std::make_unique<Session>(command_dispatcher_context()->session_id(),
                                         std::move(session_context), std::move(event_reporter),
                                         std::move(error_reporter), std::move(inspect_object))) {}

void SessionHandler::Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
                             std::vector<zx::event> release_fences,
                             fuchsia::ui::scenic::Session::PresentCallback callback) {
  if (!session_->ScheduleUpdateForPresent(zx::time(presentation_time),
                                          std::move(buffered_commands_), std::move(acquire_fences),
                                          std::move(release_fences), std::move(callback))) {
    KillSession();
  } else {
    buffered_commands_.clear();
  }
}

void SessionHandler::Present2(zx_time_t requested_presentation_time,
                              std::vector<zx::event> acquire_fences,
                              std::vector<zx::event> release_fences) {
  if (!session_->ScheduleUpdateForPresent2(
          zx::time(requested_presentation_time), std::move(buffered_commands_),
          std::move(acquire_fences), std::move(release_fences), Present2Info(session_->id()))) {
    KillSession();
  } else {
    buffered_commands_.clear();
  }
}

void SessionHandler::SetOnFramePresentedCallback(OnFramePresentedCallback callback) {
  session_->SetOnFramePresentedCallback(std::move(callback));
}

void SessionHandler::GetFuturePresentationInfos(
    zx::duration requested_prediction_span,
    scheduling::FrameScheduler::GetFuturePresentationInfosCallback callback) {
  session_->GetFuturePresentationInfos(requested_prediction_span, std::move(callback));
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
