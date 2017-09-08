// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/sketchy/app.h"

namespace sketchy_service {

App::App(escher::Escher* escher)
    : loop_(mtl::MessageLoop::GetCurrent()),
      context_(app::ApplicationContext::CreateFromStartupInfo()),
      scene_manager_(
          context_->ConnectToEnvironmentService<scenic::SceneManager>()),
      session_(std::make_unique<scenic_lib::Session>(scene_manager_.get())),
      canvas_(std::make_unique<CanvasImpl>(session_.get(), escher)) {
  context_->outgoing_services()->AddService<sketchy::Canvas>(
      [this](fidl::InterfaceRequest<sketchy::Canvas> request) {
        FTL_LOG(INFO) << "Sketchy service: accepting connection to Canvas.";
        // TODO(MZ-270): Support multiple simultaneous Canvas clients.
        bindings_.AddBinding(canvas_.get(), std::move(request));
      });
  session_->set_connection_error_handler([this] {
    FTL_LOG(INFO) << "Sketchy service lost connection to Session.";
    loop_->QuitNow();
  });
  scene_manager_.set_connection_error_handler([this] {
    FTL_LOG(INFO) << "Sketchy service lost connection to SceneManager.";
    loop_->QuitNow();
  });
}

}  // namespace sketchy_service
