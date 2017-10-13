// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "garnet/bin/ui/scene_manager/displays/display_manager.h"
#include "garnet/bin/ui/scene_manager/scene_manager_impl.h"
#include "garnet/examples/escher/common/demo.h"
#include "garnet/examples/escher/common/demo_harness_fuchsia.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/ui/scenic/fidl/scene_manager.fidl.h"

namespace scene_manager {

class SceneManagerApp {
 public:
  class Params {
   public:
    bool Setup(const fxl::CommandLine& command_line) { return true; }
  };

  // Takes ownership of |surface|.
  SceneManagerApp(Params* params,
                  DisplayManager* display_manager,
                  escher::VulkanInstancePtr vulkan_instance,
                  escher::VulkanDeviceQueuesPtr vulkan_device,
                  vk::SurfaceKHR surface);
  ~SceneManagerApp();

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;

  escher::VulkanInstancePtr vulkan_instance_;
  escher::VulkanDeviceQueuesPtr vulkan_device_queues_;
  vk::SurfaceKHR surface_;
  escher::Escher escher_;

  std::unique_ptr<SceneManagerImpl> scene_manager_;

  fidl::BindingSet<scenic::SceneManager> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SceneManagerApp);
};

}  // namespace scene_manager
