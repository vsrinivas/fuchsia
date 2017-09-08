// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "lib/ui/scenic/fidl/scene_manager.fidl.h"
#include "garnet/bin/ui/scene_manager/engine/engine.h"

namespace scene_manager {

class FrameScheduler;

class SceneManagerImpl : public scenic::SceneManager {
 public:
  explicit SceneManagerImpl(std::unique_ptr<Engine> engine);
  ~SceneManagerImpl() override;

  Engine* engine() { return engine_.get(); }

  // scenic::SceneManager interface methods.
  void CreateSession(
      ::fidl::InterfaceRequest<scenic::Session> request,
      ::fidl::InterfaceHandle<scenic::SessionListener> listener) override;
  void GetDisplayInfo(const GetDisplayInfoCallback& callback) override;

 private:
  std::unique_ptr<Engine> engine_;
};

}  // namespace scene_manager
