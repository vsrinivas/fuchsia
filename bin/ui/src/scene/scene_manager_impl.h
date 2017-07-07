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
#include "escher/examples/common/demo_harness.h"
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
      std::unique_ptr<FrameScheduler> frame_scheduler = nullptr,
      std::unique_ptr<escher::VulkanSwapchain> swapchain = nullptr);
  ~SceneManagerImpl() override;

  SessionContext* session_context() { return session_context_.get(); }

  // mozart2::SceneManager interface methods.
  void CreateSession(
      ::fidl::InterfaceRequest<mozart2::Session> request,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;
  void GetDisplayInfo(const GetDisplayInfoCallback& callback) override;

 protected:
  // Only used by subclasses used in testing.
  explicit SceneManagerImpl(std::unique_ptr<SessionContext> session_context,
                            std::unique_ptr<FrameScheduler> frame_scheduler);

 private:
  std::unique_ptr<FrameScheduler> frame_scheduler_;
  std::unique_ptr<SessionContext> session_context_;
  std::vector<mozart2::Session::PresentCallback> pending_present_callbacks_;
};

}  // namespace scene
}  // namespace mozart
