// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/session_manager.h"

namespace scenic_impl {
namespace gfx {

SessionManager::SessionManager(inspect_deprecated::Node inspect_node)
    : inspect_node_(std::move(inspect_node)) {}

Session* SessionManager::FindSession(SessionId id) const {
  auto it = session_map_.find(id);
  if (it != session_map_.end()) {
    return it->second;
  }
  return nullptr;
}

CommandDispatcherUniquePtr SessionManager::CreateCommandDispatcher(
    scheduling::SessionId session_id, SessionContext session_context,
    std::shared_ptr<EventReporter> event_reporter, std::shared_ptr<ErrorReporter> error_reporter) {
  auto inspect_node = inspect_node_.CreateChild("Session-" + std::to_string(session_id));
  Session* session = new Session(session_id, std::move(session_context), std::move(event_reporter),
                                 std::move(error_reporter), std::move(inspect_node));
  InsertSession(session_id, session);

  return CommandDispatcherUniquePtr(session,
                                    // Custom deleter.
                                    [this, session_id](CommandDispatcher* cd) {
                                      RemoveSession(session_id);
                                      delete cd;
                                    });
}

void SessionManager::InsertSession(SessionId session_id, Session* session) {
  FXL_DCHECK(session_map_.find(session_id) == session_map_.end());
  session_map_.insert({session_id, session});
}

void SessionManager::RemoveSession(SessionId id) {
  auto it = session_map_.find(id);
  if (it != session_map_.end()) {
    session_map_.erase(it);
  }
}

}  // namespace gfx
}  // namespace scenic_impl
