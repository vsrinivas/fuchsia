// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "application/services/application_environment.fidl.h"
#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/src/scene_manager/display.h"
#include "apps/mozart/src/scene_manager/scene_manager_impl.h"
#include "lib/escher/examples/common/demo.h"
#include "lib/escher/examples/common/demo_harness_fuchsia.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"

namespace scene_manager {

// TODO(MZ-142): SceneManagerApp shouldn't inherit from Demo.
class SceneManagerApp {
 public:
  class Params {
   public:
    bool Setup(const ftl::CommandLine& command_line) { return true; }
  };

  SceneManagerApp(app::ApplicationContext* app_context,
                  uint32_t width,
                  uint32_t height,
                  float device_pixel_ratio,
                  Params* params,
                  std::unique_ptr<DemoHarness> harness);
  ~SceneManagerApp();

 private:
  app::ApplicationContext* const application_context_;

  std::unique_ptr<DemoHarness> demo_harness_;
  escher::VulkanContext vulkan_context_;
  escher::Escher escher_;
  Display display_;
  std::unique_ptr<SceneManagerImpl> scene_manager_;

  fidl::BindingSet<mozart2::SceneManager> bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneManagerApp);
};

}  // namespace scene_manager
