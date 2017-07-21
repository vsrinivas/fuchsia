// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/services/scene/session.fidl.h"
#include "apps/mozart/src/scene_manager/display.h"
#include "apps/mozart/src/scene_manager/frame_scheduler.h"
#include "apps/mozart/src/scene_manager/session/engine.h"
#include "escher/forward_declarations.h"

namespace scene_manager {

class FrameScheduler;

class SceneManagerImpl : public mozart2::SceneManager {
 public:
  explicit SceneManagerImpl(
      Display* display,
      escher::Escher* escher = nullptr,
      std::unique_ptr<FrameScheduler> frame_scheduler = nullptr,
      std::unique_ptr<escher::VulkanSwapchain> swapchain = nullptr);
  ~SceneManagerImpl() override;

  Engine* engine() { return engine_.get(); }

  // mozart2::SceneManager interface methods.
  void CreateSession(
      ::fidl::InterfaceRequest<mozart2::Session> request,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;
  void GetDisplayInfo(const GetDisplayInfoCallback& callback) override;

 protected:
  // Only used by subclasses used in testing.
  explicit SceneManagerImpl(Display* display, std::unique_ptr<Engine> engine);

 private:
  Display* display_;
  std::unique_ptr<Engine> engine_;
};

}  // namespace scene_manager
