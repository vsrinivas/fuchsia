// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/services/scene/session.fidl.h"
#include "apps/mozart/src/scene/frame_scheduler.h"
#include "apps/mozart/src/scene/session/session.h"
#include "apps/mozart/src/scene/session/session_handler.h"
#include "escher/forward_declarations.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"

namespace mozart {
namespace scene {

class FrameScheduler;

class SceneManagerImpl : public mozart2::SceneManager {
 public:
  explicit SceneManagerImpl(
      escher::Escher* escher = nullptr,
      std::unique_ptr<FrameScheduler> frame_scheduler = nullptr);

  ~SceneManagerImpl() override;

  SessionContext& session_context() { return session_context_; }

  // mozart2::SceneManager interface methods.
  void CreateSession(
      ::fidl::InterfaceRequest<mozart2::Session> request,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;

  size_t GetSessionCount() { return session_count_; }

  SessionHandler* FindSession(SessionId id);

 private:
  friend class SessionHandler;

  void TearDownSession(SessionId id);

  // Allow overriding to support tests.
  virtual std::unique_ptr<SessionHandler> CreateSessionHandler(
      SessionId id,
      ::fidl::InterfaceRequest<mozart2::Session> request,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener);

  std::unique_ptr<FrameScheduler> frame_scheduler_;
  SessionContext session_context_;
  std::unordered_map<SessionId, std::unique_ptr<SessionHandler>> sessions_;
  std::atomic<size_t> session_count_;
  std::vector<mozart2::Session::PresentCallback> pending_present_callbacks_;
  SessionId next_session_id_ = 1;
};

}  // namespace scene
}  // namespace mozart
