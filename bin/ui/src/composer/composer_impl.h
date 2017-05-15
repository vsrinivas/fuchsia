// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/mozart/services/composer/composer.fidl.h"
#include "apps/mozart/services/composer/session.fidl.h"
#include "apps/mozart/src/composer/resources/link.h"
#include "apps/mozart/src/composer/session/session.h"
#include "apps/mozart/src/composer/session/session_handler.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"

namespace mozart {
namespace composer {

class Renderer;

class ComposerImpl : public mozart2::Composer, public SessionContext {
 public:
  ComposerImpl();
  ~ComposerImpl() override;

  // mozart2::Composer interface methods.
  void CreateSession(
      ::fidl::InterfaceRequest<mozart2::Session> request) override;

  // SessionContext interface methods.
  LinkPtr CreateLink(Session* session, const mozart2::LinkPtr& args) override;
  void OnSessionTearDown(Session* session) override;

  size_t GetSessionCount() { return session_count_; }

  const std::vector<LinkPtr>& links() const { return links_; }

  Renderer* renderer() const { return renderer_.get(); }

 private:
  friend class SessionHandler;
  void ApplySessionUpdate(std::unique_ptr<SessionUpdate> update);

  void TearDownSession(SessionId id);

  std::unordered_map<SessionId, std::unique_ptr<SessionHandler>> sessions_;
  std::atomic<size_t> session_count_;

  // Placeholders for Links and the Renderer. These will be instantiated
  // differently in the future.
  std::vector<LinkPtr> links_;
  std::unique_ptr<Renderer> renderer_;

  SessionId next_session_id_ = 1;
};

}  // namespace composer
}  // namespace mozart
