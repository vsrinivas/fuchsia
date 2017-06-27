// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/scene_manager_app.h"

#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace scene {

SceneManagerApp::SceneManagerApp(Params* params,
                                 DemoHarnessFuchsia* demo_harness)
    : Demo(demo_harness),
      application_context_(demo_harness->application_context()),
      scene_manager_(
          escher(),
          std::make_unique<FrameScheduler>(escher(),
                                           harness()->GetVulkanSwapchain(),
                                           &display_)) {
  FTL_DCHECK(application_context_);

  tracing::InitializeTracer(application_context_, {"scene_manager"});

  application_context_->outgoing_services()->AddService<mozart2::SceneManager>(
      [this](fidl::InterfaceRequest<mozart2::SceneManager> request) {
        FTL_LOG(INFO) << "Accepting connection to SceneManagerImpl";
        bindings_.AddBinding(&scene_manager_, std::move(request));
      });
}

SceneManagerApp::~SceneManagerApp() {}

void SceneManagerApp::DrawFrame() {
  // We only subclass from Demo to get access to Vulkan/Escher, not for the
  // rendering framework.
  FTL_DCHECK(false);
}

}  // namespace scene
}  // namespace mozart
