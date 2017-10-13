// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/scene_manager_app.h"

#include "lib/fxl/logging.h"

namespace scene_manager {

SceneManagerApp::SceneManagerApp(
    Params* params,
    DisplayManager* display_manager,
    escher::VulkanInstancePtr vulkan_instance,
    escher::VulkanDeviceQueuesPtr vulkan_device_queues,
    vk::SurfaceKHR surface)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      vulkan_instance_(vulkan_instance),
      vulkan_device_queues_(vulkan_device_queues),
      surface_(surface),
      escher_(vulkan_device_queues_),
      scene_manager_(std::make_unique<SceneManagerImpl>(
          std::make_unique<Engine>(display_manager, &escher_))) {
  FXL_DCHECK(application_context_);

  application_context_->outgoing_services()->AddService<scenic::SceneManager>(
      [this](fidl::InterfaceRequest<scenic::SceneManager> request) {
        FXL_LOG(INFO) << "Accepting connection to SceneManagerImpl";
        bindings_.AddBinding(scene_manager_.get(), std::move(request));
      });
}

SceneManagerApp::~SceneManagerApp() {
  if (surface_) {
    vulkan_instance_->vk_instance().destroySurfaceKHR(surface_);
  }
  surface_ = nullptr;
}

}  // namespace scene_manager
