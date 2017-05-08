// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/composer_impl.h"

#include "lib/ftl/functional/make_copyable.h"

namespace mozart {
namespace composer {

ComposerImpl::ComposerImpl() : session_count_(0) {}

ComposerImpl::~ComposerImpl() {}

void ComposerImpl::CreateSession(
    ::fidl::InterfaceRequest<mozart2::Session> request) {
  SessionId session_id = next_session_id_++;

  auto handler =
      std::make_unique<SessionHandler>(this, session_id, std::move(request));
  sessions_.insert({session_id, std::move(handler)});
  ++session_count_;
}

void ComposerImpl::ApplySessionUpdate(std::unique_ptr<SessionUpdate> update) {
  auto& session = update->session;
  if (session->is_valid()) {
    for (auto& op : update->ops) {
      if (!session->ApplyOp(op)) {
        FTL_LOG(WARNING) << "mozart::Compositor::ComposerImpl::"
                            "ApplySessionUpdate() initiating teardown";
        TearDownSession(session->id());
        return;
      }
    }
  }
}

void ComposerImpl::TearDownSession(SessionId id) {
  auto it = sessions_.find(id);
  FTL_DCHECK(it != sessions_.end());
  if (it != sessions_.end()) {
    std::unique_ptr<SessionHandler> handler = std::move(it->second);
    sessions_.erase(it);
    --session_count_;
    handler->TearDown();
  }
}

LinkPtr ComposerImpl::CreateLink(Session* session,
                                 const mozart2::LinkPtr& args) {
  session->error_reporter()->ERROR()
      << "SessionContext::CreateLink() unimplemented";
  return LinkPtr();
}

}  // namespace composer
}  // namespace mozart
