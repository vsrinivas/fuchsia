// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SESSION_MANAGER_H_
#define GARNET_LIB_UI_GFX_ENGINE_SESSION_MANAGER_H_

#include <lib/inspect/inspect.h>

#include <unordered_map>

#include "garnet/lib/ui/gfx/engine/session_context.h"
#include "garnet/lib/ui/scenic/command_dispatcher.h"

namespace scenic_impl {
class EventReporter;
class ErrorReporter;
}  // namespace scenic_impl

namespace scenic_impl {
namespace gfx {

using SessionId = ::scenic_impl::SessionId;

class SessionHandler;

// Manages a collection of SessionHandlers.
// Tracks future updates requested by Sessions, and executes updates for a
// particular presentation time.
class SessionManager {
 public:
  explicit SessionManager(inspect::Object inspect_object = inspect::Object());

  virtual ~SessionManager() = default;

  // Finds and returns a pointer the session handler corresponding to the given
  // |id|. Returns nullptr if none found.
  SessionHandler* FindSessionHandler(SessionId id) const;

  size_t GetSessionCount() { return session_count_; }

  // Returns a SessionHandler, which is casted as a CommandDispatcher. Used by
  // ScenicSystem.
  CommandDispatcherUniquePtr CreateCommandDispatcher(
      CommandDispatcherContext dispatcher_context,
      SessionContext session_context);

 private:
  // Insert a SessionHandler into the |session_handlers_| map
  void InsertSessionHandler(SessionId session_id,
                            SessionHandler* session_handler);

  // Removes the SessionHandler from the |session_handlers_| map. Only called by
  // the custom deleter provided by CreateCommandDispatcher.
  void RemoveSessionHandler(SessionId id);

  // Virtual for testing purposes
  virtual std::unique_ptr<SessionHandler> CreateSessionHandler(
      CommandDispatcherContext dispatcher_context,
      SessionContext session_context, SessionId session_id,
      EventReporter* event_reporter, ErrorReporter* error_reporter);

  // Map of all the sessions.
  std::unordered_map<SessionId, SessionHandler*> session_handlers_;
  size_t session_count_ = 0;

  inspect::Object inspect_object_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_SESSION_MANAGER_H_
