// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/root_presenter/app.h"

#include <algorithm>

#include "application/lib/app/connect.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/src/root_presenter/presentation.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/logging.h"

namespace root_presenter {

App::App(const ftl::CommandLine& command_line)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  FTL_DCHECK(application_context_);

  tracing::InitializeTracer(application_context_.get(), {"root_presenter"});

  application_context_->outgoing_services()->AddService<mozart::Presenter>(
      [this](fidl::InterfaceRequest<mozart::Presenter> request) {
        presenter_bindings_.AddBinding(this, std::move(request));
      });
}

App::~App() {}

void App::Present(fidl::InterfaceHandle<mozart::ViewOwner> view_owner_handle) {
  InitializeServices();

  auto presentation = std::make_unique<Presentation>(
      compositor_.get(), view_manager_.get(),
      mozart::ViewOwnerPtr::Create(std::move(view_owner_handle)));
  presentation->Present([ this, presentation = presentation.get() ] {
    auto it = std::find_if(
        presentations_.begin(), presentations_.end(),
        [presentation](const std::unique_ptr<Presentation>& other) {
          return other.get() == presentation;
        });
    FTL_DCHECK(it != presentations_.end());
    presentations_.erase(it);
  });
  presentations_.push_back(std::move(presentation));
}

void App::InitializeServices() {
  if (!compositor_) {
    application_context_->ConnectToEnvironmentService(compositor_.NewRequest());
    compositor_.set_connection_error_handler([this] {
      FTL_LOG(ERROR) << "Compositor died, destroying view trees.";
      Reset();
    });
  }

  if (!view_manager_) {
    application_context_->ConnectToEnvironmentService(
        view_manager_.NewRequest());
    view_manager_.set_connection_error_handler([this] {
      FTL_LOG(ERROR) << "ViewManager died, destroying view trees.";
      Reset();
    });
  }
}

void App::Reset() {
  presentations_.clear();  // must be first, holds pointers to services
  compositor_.reset();
  view_manager_.reset();
}

}  // namespace root_presenter
