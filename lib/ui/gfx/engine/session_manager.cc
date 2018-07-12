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

namespace scenic {
namespace gfx {

SessionHandler* SessionManager::FindSession(SessionId id) {
  auto it = session_manager_.find(id);
  if (it != session_manager_.end()) {
    return it->second;
  }
  return nullptr;
}

std::unique_ptr<SessionHandler> SessionManager::CreateSessionHandler(
    CommandDispatcherContext context, Engine* engine, SessionId session_id,
    EventReporter* event_reporter, ErrorReporter* error_reporter) const {
  return std::make_unique<SessionHandler>(
      std::move(context), engine, session_id, event_reporter, error_reporter);
}

std::unique_ptr<CommandDispatcher> SessionManager::CreateCommandDispatcher(
    CommandDispatcherContext context, Engine* engine) {
  SessionId session_id = next_session_id_++;

  scenic::Session* session = context.session();
  auto handler = CreateSessionHandler(std::move(context), engine, session_id,
                                      session, session->error_reporter());
  session_manager_.insert({session_id, handler.get()});
  ++session_count_;

  return handler;
}

void SessionManager::ScheduleUpdateForSession(UpdateScheduler* update_scheduler,
                                              uint64_t presentation_time,
                                              fxl::RefPtr<Session> session) {
  FXL_DCHECK(update_scheduler);
  if (session->is_valid()) {
    updatable_sessions_.insert({presentation_time, std::move(session)});
    update_scheduler->ScheduleUpdate(presentation_time);
  }
}

bool SessionManager::ApplyScheduledSessionUpdates(
    uint64_t presentation_time, uint64_t presentation_interval) {
  TRACE_DURATION("gfx", "ApplyScheduledSessionUpdates", "time",
                 presentation_time, "interval", presentation_interval);

  bool needs_render = false;
  while (!updatable_sessions_.empty()) {
    auto top = updatable_sessions_.begin();
    if (top->first > presentation_time)
      break;
    auto session = std::move(top->second);
    updatable_sessions_.erase(top);
    if (session) {
      needs_render |= session->ApplyScheduledUpdates(presentation_time,
                                                     presentation_interval);
    } else {
      // Corresponds to a call to ScheduleUpdate(), which always triggers a
      // render.
      needs_render = true;
    }
  }
  return needs_render;
}

void SessionManager::TearDownSession(SessionId id) {
  auto it = session_manager_.find(id);
  FXL_DCHECK(it != session_manager_.end());
  if (it != session_manager_.end()) {
    SessionHandler* handler = std::move(it->second);
    session_manager_.erase(it);
    FXL_DCHECK(session_count_ > 0);
    --session_count_;

    // Don't destroy handler immediately, since it may be the one calling
    // TearDownSession().
    async::PostTask(async_get_default_dispatcher(), [handler] { handler->TearDown(); });
  }
}

}  // namespace gfx
}  // namespace scenic
