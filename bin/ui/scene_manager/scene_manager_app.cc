// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/scene_manager_app.h"

#include "lib/ftl/logging.h"

namespace scene_manager {

SceneManagerApp::SceneManagerApp(app::ApplicationContext* app_context,
                                 Params* params,
                                 DisplayManager* display_manager,
                                 std::unique_ptr<DemoHarness> demo_harness)
    : application_context_(app_context),
      demo_harness_(std::move(demo_harness)),
      escher_(demo_harness_->device_queues()),
      scene_manager_(std::make_unique<SceneManagerImpl>(
          std::make_unique<Engine>(display_manager,
                                   &escher_,
                                   std::make_unique<escher::VulkanSwapchain>(
                                       demo_harness_->GetVulkanSwapchain())))) {
  FTL_DCHECK(application_context_);

  application_context_->outgoing_services()->AddService<scenic::SceneManager>(
      [this](fidl::InterfaceRequest<scenic::SceneManager> request) {
        FTL_LOG(INFO) << "Accepting connection to SceneManagerImpl";
        bindings_.AddBinding(scene_manager_.get(), std::move(request));
      });
}

SceneManagerApp::~SceneManagerApp() {}

}  // namespace scene_manager
