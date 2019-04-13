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

SessionManager::SessionManager(inspect::Object inspect_object)
    : inspect_object_(std::move(inspect_object)) {}

SessionHandler* SessionManager::FindSessionHandler(SessionId id) const {
  auto it = session_handlers_.find(id);
  if (it != session_handlers_.end()) {
    return it->second;
  }
  return nullptr;
}

CommandDispatcherUniquePtr SessionManager::CreateCommandDispatcher(
    CommandDispatcherContext dispatcher_context,
    SessionContext session_context) {
  scenic_impl::Session* session = dispatcher_context.session();
  auto handler = this->CreateSessionHandler(
      std::move(dispatcher_context), std::move(session_context), session->id(),
      session, session->error_reporter());
  InsertSessionHandler(session->id(), handler.get());

  return CommandDispatcherUniquePtr(
      handler.release(),
      // Custom deleter.
      [this, id = session->id()](CommandDispatcher* cd) {
        RemoveSessionHandler(id);
        delete cd;
      });
}

std::unique_ptr<SessionHandler> SessionManager::CreateSessionHandler(
    CommandDispatcherContext dispatcher_context, SessionContext session_context,
    SessionId session_id, EventReporter* event_reporter,
    ErrorReporter* error_reporter) {
  auto inspect_object = inspect_object_.CreateChild("Session-" + std::to_string(session_id));
  return std::make_unique<SessionHandler>(std::move(dispatcher_context),
                                          std::move(session_context),
                                          event_reporter, error_reporter,
                                          std::move(inspect_object));
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

}  // namespace gfx
}  // namespace scenic_impl
