// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SESSION_MANAGER_H_
#define GARNET_LIB_UI_GFX_ENGINE_SESSION_MANAGER_H_

#include <unordered_map>

#include "garnet/lib/ui/scenic/command_dispatcher.h"

namespace scenic_impl {
class EventReporter;
class ErrorReporter;
}  // namespace scenic_impl

namespace scenic_impl {
namespace gfx {

using SessionId = ::scenic_impl::SessionId;

class SessionHandler;
class Engine;

// Manages a collection of SessionHandlers.
// Tracks future updates requested by Sessions, and executes updates for a
// particular presentation time.
class SessionManager {
 public:
  SessionManager() = default;

  virtual ~SessionManager() = default;

  // Finds the session handler corresponding to the given id.
  SessionHandler* FindSessionHandler(SessionId id);

  size_t GetSessionCount() { return session_count_; }

  // Returns a SessionHandler, which is casted as a CommandDispatcher. Used by
  // ScenicSystem.
  std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context, Engine* engine);

  // Called by engine on a failed session update
  void KillSession(SessionId session_id);

 protected:
  // Protected for testing subclass
  void InsertSessionHandler(SessionId session_id,
                            SessionHandler* session_handler);

  virtual std::unique_ptr<SessionHandler> CreateSessionHandler(
      CommandDispatcherContext context, Engine* engine,
      EventReporter* event_reporter, ErrorReporter* error_reporter) const;

 private:
  friend class SessionHandler;

  // Removes the SessionHandler from the session_handlers_ map.  We assume that
  // the SessionHandler has already taken care of itself and its Session.
  void RemoveSessionHandler(SessionId id);

  // Map of all the sessions.
  std::unordered_map<SessionId, SessionHandler*> session_handlers_;
  size_t session_count_ = 0;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_SESSION_MANAGER_H_
