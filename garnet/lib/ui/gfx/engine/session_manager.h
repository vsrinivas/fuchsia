// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SESSION_MANAGER_H_
#define GARNET_LIB_UI_GFX_ENGINE_SESSION_MANAGER_H_

#include <set>
#include <unordered_map>

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/scenic/command_dispatcher.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"

namespace scenic_impl {
class EventReporter;
class ErrorReporter;
}  // namespace scenic_impl

namespace scenic_impl {
namespace gfx {

using SessionId = ::scenic_impl::SessionId;
using PresentationTime = uint64_t;

class SessionHandler;
class Engine;
class UpdateScheduler;

// Graphical context for a set of session updates.
// The CommandContext is only valid during RenderFrame() and should not be
// accessed outside of that.
class CommandContext {
 public:
  CommandContext(std::unique_ptr<escher::BatchGpuUploader> uploader);

  escher::BatchGpuUploader* batch_gpu_uploader() const {
    return batch_gpu_uploader_.get();
  }

  // Flush any work accumulated during command processing.
  void Flush();

 private:
  std::unique_ptr<escher::BatchGpuUploader> batch_gpu_uploader_;
};

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

  // Tell the UpdateScheduler to schedule a frame, and remember the Session so
  // that we can tell it to apply updates in ApplyScheduledSessionUpdates().
  void ScheduleUpdateForSession(
      UpdateScheduler* update_scheduler, uint64_t presentation_time,
      fxl::WeakPtr<scenic_impl::gfx::Session> session);

  // Executes updates that are schedule up to and including a given presentation
  // time. Returns true if rendering is needed.
  bool ApplyScheduledSessionUpdates(CommandContext* command_context,
                                    uint64_t presentation_time,
                                    uint64_t presentation_interval);

 protected:
  // Protected for testing subclass
  void InsertSessionHandler(SessionId session_id,
                            SessionHandler* session_handler);

 private:
  friend class SessionHandler;

  // Used to compare presentation times so that the priority_queue acts as a min
  // heap, placing the earliest PresentationTime at the top
  class UpdatableSessionsComparator {
   public:
    bool operator()(
        std::pair<PresentationTime, fxl::WeakPtr<Session>> updatable_session1,
        std::pair<PresentationTime, fxl::WeakPtr<Session>> updatable_session2) {
      return updatable_session1.first > updatable_session2.first;
    }
  };

  // Removes the SessionHandler from the session_handlers_ map.  We assume that
  // the SessionHandler has already taken care of itself and its Session.
  void RemoveSessionHandler(SessionId id);

  virtual std::unique_ptr<SessionHandler> CreateSessionHandler(
      CommandDispatcherContext context, Engine* engine, SessionId session_id,
      EventReporter* event_reporter, ErrorReporter* error_reporter) const;

  // Map of all the sessions.
  std::unordered_map<SessionId, SessionHandler*> session_handlers_;
  size_t session_count_ = 0;
  SessionId next_session_id_ = 1;

  // Lists all Session that have updates to apply, sorted by the earliest
  // requested presentation time of each update.
  std::priority_queue<
      std::pair<PresentationTime, fxl::WeakPtr<Session>>,
      std::vector<std::pair<PresentationTime, fxl::WeakPtr<Session>>>,
      UpdatableSessionsComparator>
      updatable_sessions_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_SESSION_MANAGER_H_
