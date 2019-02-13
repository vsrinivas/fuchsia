// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/scenic/session.h"

namespace scenic_impl {
namespace gfx {

SessionHandler* SessionManager::FindSessionHandler(SessionId id) {
  auto it = session_handlers_.find(id);
  if (it != session_handlers_.end()) {
    return it->second;
  }
  return nullptr;
}

std::unique_ptr<SessionHandler> SessionManager::CreateSessionHandler(
    CommandDispatcherContext context, Engine* engine,
    EventReporter* event_reporter, ErrorReporter* error_reporter) const {
  return std::make_unique<SessionHandler>(
      std::move(context), engine->session_manager(), engine->session_context(),
      event_reporter, error_reporter);
}

std::unique_ptr<CommandDispatcher> SessionManager::CreateCommandDispatcher(
    CommandDispatcherContext context, Engine* engine) {
  scenic_impl::Session* session = context.session();
  auto handler = CreateSessionHandler(std::move(context), engine, session,
                                      session->error_reporter());
  InsertSessionHandler(session->id(), handler.get());
  return handler;
}

void SessionManager::InsertSessionHandler(SessionId session_id,
                                          SessionHandler* session_handler) {
  FXL_DCHECK(session_handlers_.find(session_id) == session_handlers_.end());
  session_handlers_.insert({session_id, session_handler});
  ++session_count_;
}

void SessionManager::RemoveSessionHandler(SessionId id) {
  auto it = session_handlers_.find(id);
  if (it != session_handlers_.end()) {
    session_handlers_.erase(it);
    FXL_DCHECK(session_count_ > 0);
    --session_count_;
  }
}

void SessionManager::KillSession(SessionId session_id) {
  auto session_handler = FindSessionHandler(session_id);
  FXL_DCHECK(session_handler);
  session_handler->BeginTearDown();
}

}  // namespace gfx
}  // namespace scenic_impl
