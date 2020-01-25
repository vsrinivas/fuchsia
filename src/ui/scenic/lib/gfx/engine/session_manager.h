// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_MANAGER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_MANAGER_H_

#include <unordered_map>

#include "src/lib/inspect_deprecated/inspect.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/engine/session_context.h"
#include "src/ui/scenic/lib/scenic/command_dispatcher.h"

namespace scenic_impl {
class EventReporter;
class ErrorReporter;
}  // namespace scenic_impl

namespace scenic_impl {
namespace gfx {

// Manages a collection of Sessions.
// Tracks future updates requested by Sessions, and executes updates for a
// particular presentation time.
class SessionManager {
 public:
  explicit SessionManager(inspect_deprecated::Node inspect_node = inspect_deprecated::Node());

  virtual ~SessionManager() = default;

  // Finds and returns a pointer the session handler corresponding to the given
  // |id|. Returns nullptr if none found.
  gfx::Session* FindSession(scheduling::SessionId id) const;
  const std::unordered_map<scheduling::SessionId, gfx::Session*>& sessions() {
    return session_map_;
  }

  // Returns a Session, which is casted as a CommandDispatcher. Used by
  // ScenicSystem.
  CommandDispatcherUniquePtr CreateCommandDispatcher(scheduling::SessionId session_id,
                                                     SessionContext session_context,
                                                     std::shared_ptr<EventReporter> event_reporter,
                                                     std::shared_ptr<ErrorReporter> error_reporter);

 private:
  // Insert a Session into the |session_map_|.
  void InsertSession(scheduling::SessionId session_id, gfx::Session* session);

  // Removes the Session from the |session_map_|. Only called by
  // the custom deleter provided by CreateCommandDispatcher.
  void RemoveSession(scheduling::SessionId id);

  // Map of all the sessions.
  std::unordered_map<scheduling::SessionId, Session*> session_map_;

  inspect_deprecated::Node inspect_node_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_MANAGER_H_
