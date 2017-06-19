// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/scene_manager_impl.h"

#include "apps/mozart/src/scene/display.h"

#include "lib/ftl/functional/make_copyable.h"

namespace mozart {
namespace scene {

SceneManagerImpl::SceneManagerImpl(
    escher::Escher* escher,
    std::unique_ptr<FrameScheduler> frame_scheduler,
    std::unique_ptr<escher::VulkanSwapchain> swapchain)
    : frame_scheduler_(std::move(frame_scheduler)),
      session_context_(std::make_unique<SessionContext>(escher,
                                                        frame_scheduler_.get(),
                                                        std::move(swapchain))),
      session_count_(0) {
  // Either both Escher and a FrameScheduler must be available, or neither.
  FTL_DCHECK(!escher == !frame_scheduler_);

  // If a FrameScheduler was created, introduce it to the SessionContext.
  if (frame_scheduler_) {
    frame_scheduler_->AddListener(session_context_.get());
  }
}

SceneManagerImpl::~SceneManagerImpl() {
  if (frame_scheduler_) {
    frame_scheduler_->RemoveListener(session_context_.get());
  }
}

SceneManagerImpl::SceneManagerImpl(
    std::unique_ptr<SessionContext> session_context,
    std::unique_ptr<FrameScheduler> frame_scheduler)
    : frame_scheduler_(std::move(frame_scheduler)),
      session_context_(std::move(session_context)),
      session_count_(0) {}

void SceneManagerImpl::CreateSession(
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  SessionId session_id = next_session_id_++;

  auto handler =
      CreateSessionHandler(session_id, std::move(request), std::move(listener));
  sessions_.insert({session_id, std::move(handler)});
  ++session_count_;
}

void SceneManagerImpl::GetDisplayInfo(const GetDisplayInfoCallback& callback) {
  // TODO(MZ-16): need to specify different device pixel ratio for NUC vs.
  // Acer Switch 12, and also not hardcode width/height.
  auto info = mozart2::DisplayInfo::New();
  info->width = Display::kHardcodedDisplayWidth;
  info->height = Display::kHardcodedDisplayHeight;
  info->device_pixel_ratio = Display::kHardcodedDevicePixelRatio;
  callback(std::move(info));
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

}  // namespace scene
}  // namespace mozart
