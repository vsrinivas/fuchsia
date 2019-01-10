// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/engine/update_scheduler.h"
#include "garnet/lib/ui/scenic/session.h"

namespace scenic_impl {
namespace gfx {

CommandContext::CommandContext(
    std::unique_ptr<escher::BatchGpuUploader> uploader)
    : batch_gpu_uploader_(std::move(uploader)) {}

void CommandContext::Flush() {
  if (batch_gpu_uploader_) {
    // Submit regardless of whether or not there are updates to release the
    // underlying CommandBuffer so the pool and sequencer don't stall out.
    // TODO(ES-115) to remove this restriction.
    batch_gpu_uploader_->Submit();
  }
}

SessionHandler* SessionManager::FindSessionHandler(SessionId id) {
  auto it = session_handlers_.find(id);
  if (it != session_handlers_.end()) {
    return it->second;
  }
  return nullptr;
}

std::unique_ptr<SessionHandler> SessionManager::CreateSessionHandler(
    CommandDispatcherContext context, Engine* engine, SessionId session_id,
    EventReporter* event_reporter, ErrorReporter* error_reporter) const {
  return std::make_unique<SessionHandler>(
      std::move(context), engine->session_manager(), engine->session_context(),
      session_id, event_reporter, error_reporter);
}

std::unique_ptr<CommandDispatcher> SessionManager::CreateCommandDispatcher(
    CommandDispatcherContext context, Engine* engine) {
  SessionId session_id = next_session_id_++;

  scenic_impl::Session* session = context.session();
  auto handler = CreateSessionHandler(std::move(context), engine, session_id,
                                      session, session->error_reporter());
  InsertSessionHandler(session_id, handler.get());
  return handler;
}

void SessionManager::InsertSessionHandler(SessionId session_id,
                                          SessionHandler* session_handler) {
  FXL_DCHECK(session_handlers_.find(session_id) == session_handlers_.end());
  session_handlers_.insert({session_id, session_handler});
  ++session_count_;
}

void SessionManager::ScheduleUpdateForSession(
    UpdateScheduler* update_scheduler, uint64_t presentation_time,
    fxl::WeakPtr<scenic_impl::gfx::Session> session) {
  FXL_DCHECK(update_scheduler);
  if (session) {
    updatable_sessions_.push({presentation_time, std::move(session)});
    update_scheduler->ScheduleUpdate(presentation_time);
  }
}

bool SessionManager::ApplyScheduledSessionUpdates(
    CommandContext* command_context, uint64_t presentation_time,
    uint64_t presentation_interval) {
  TRACE_DURATION("gfx", "ApplyScheduledSessionUpdates", "time",
                 presentation_time, "interval", presentation_interval);

  bool needs_render = false;
  while (!updatable_sessions_.empty()) {
    auto& top = updatable_sessions_.top();
    if (top.first > presentation_time)
      break;
    auto session = std::move(top.second);
    updatable_sessions_.pop();
    if (session.get() != nullptr) {
      auto update_results = session->ApplyScheduledUpdates(
          command_context, presentation_time, presentation_interval);

      needs_render |= update_results.needs_render;

      // If update fails, kill the entire client session
      if (!update_results.success) {
        auto session_handler = FindSessionHandler(session->id());
        FXL_DCHECK(session_handler);
        session_handler->BeginTearDown();
      }
    } else {
      // Corresponds to a call to ScheduleUpdate(), which always triggers a
      // render.
      needs_render = true;
    }
  }
  return needs_render;
}

void SessionManager::RemoveSessionHandler(SessionId id) {
  auto it = session_handlers_.find(id);
  if (it != session_handlers_.end()) {
    session_handlers_.erase(it);
    FXL_DCHECK(session_count_ > 0);
    --session_count_;
  }
}

}  // namespace gfx
}  // namespace scenic_impl
