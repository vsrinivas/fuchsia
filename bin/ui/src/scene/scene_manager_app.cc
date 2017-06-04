// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/scene_manager_app.h"

#include "apps/mozart/src/scene/scene_manager_impl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace scene {

SceneManagerApp::SceneManagerApp(Params* params)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  tracing::InitializeTracer(application_context_.get(), {"scene_manager"});

  application_context_->outgoing_services()->AddService<mozart2::SceneManager>(
      [this](fidl::InterfaceRequest<mozart2::SceneManager> request) {
        FTL_LOG(INFO) << "Accepting connection to new SceneManagerImpl";
        bindings_.AddBinding(std::make_unique<SceneManagerImpl>(),
                             std::move(request));
      });
}

SceneManagerApp::~SceneManagerApp() {}

}  // namespace scene
}  // namespace mozart
