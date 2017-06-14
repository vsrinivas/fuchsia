// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/scene_manager_impl.h"

#include "apps/mozart/src/scene/renderer/renderer.h"
#include "lib/ftl/functional/make_copyable.h"

namespace {
// TODO(MZ-124): We should derive an appropriate value from the rendering
// targets, in particular giving priority to couple to the display refresh
// (vsync).
constexpr uint64_t kHardcodedPresentationIntervalNanos = 16'666'667;
}  // namespace

namespace mozart {
namespace scene {

SceneManagerImpl::SceneManagerImpl()
    : session_context_(),
      session_count_(0),
      renderer_(std::make_unique<Renderer>()) {}

SceneManagerImpl::SceneManagerImpl(escher::Escher* escher)
    : session_context_(escher),
      session_count_(0),
      renderer_(std::make_unique<Renderer>()) {}

SceneManagerImpl::~SceneManagerImpl() {}

void SceneManagerImpl::CreateSession(
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  SessionId session_id = next_session_id_++;

  auto handler =
      CreateSessionHandler(session_id, std::move(request), std::move(listener));
  sessions_.insert({session_id, std::move(handler)});
  ++session_count_;
}

std::unique_ptr<SessionHandler> SceneManagerImpl::CreateSessionHandler(
    SessionId session_id,
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  return std::make_unique<SessionHandler>(this, session_id, std::move(request),
                                          std::move(listener));
}

SessionHandler* SceneManagerImpl::FindSession(SessionId id) {
  auto it = sessions_.find(id);
  if (it != sessions_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void SceneManagerImpl::ApplySessionUpdate(
    std::unique_ptr<SessionUpdate> update) {
  // TODO(MX-125): Schedule the update to be applied on or after its
  // presentation time.  May require a certain amount of queuing.  We should
  // also set limits on how much state can be retained in the queue.
  pending_present_callbacks_.push_back(std::move(update->present_callback));

  auto& session = update->session;
  if (session->is_valid()) {
    for (auto& op : update->ops) {
      if (!session->ApplyOp(op)) {
        FTL_LOG(WARNING) << "mozart::Compositor::SceneManagerImpl::"
                            "ApplySessionUpdate() initiating teardown";
        TearDownSession(session->id());
        return;
      }
    }
  }
}

void SceneManagerImpl::TearDownSession(SessionId id) {
  auto it = sessions_.find(id);
  FTL_DCHECK(it != sessions_.end());
  if (it != sessions_.end()) {
    std::unique_ptr<SessionHandler> handler = std::move(it->second);
    sessions_.erase(it);
    --session_count_;
    handler->TearDown();

    // Don't destroy handler immediately, since it may be the one calling
    // TearDownSession().
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        ftl::MakeCopyable([handler = std::move(handler)]{}));
  }
}

void SceneManagerImpl::BeginFrame() {
  mozart2::PresentationInfo info;
  info.presentation_time = mx_time_get(CLOCK_MONOTONIC);
  info.presentation_interval = kHardcodedPresentationIntervalNanos;

  for (const auto& cb : pending_present_callbacks_) {
    cb(info.Clone());
  }
  pending_present_callbacks_.clear();
}

}  // namespace scene
}  // namespace mozart
