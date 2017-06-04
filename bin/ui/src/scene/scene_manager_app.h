// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "application/services/application_environment.fidl.h"
#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"

namespace mozart {
namespace scene {

class SceneManagerImpl;

class SceneManagerApp {
 public:
  class Params {
   public:
    bool Setup(const ftl::CommandLine& command_line) { return true; }
  };

  explicit SceneManagerApp(Params* params);
  ~SceneManagerApp();

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;

  fidl::BindingSet<mozart2::SceneManager, std::unique_ptr<SceneManagerImpl>>
      bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneManagerApp);
};

}  // namespace scene
}  // namespace mozart
