// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_manager_app.h"

#include "application/lib/app/connect.h"
#include "apps/mozart/src/view_manager/view_manager_impl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/logging.h"

namespace view_manager {

ViewManagerApp::ViewManagerApp(Params* params)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  FTL_DCHECK(application_context_);

  tracing::InitializeTracer(application_context_.get(), {"view_manager"});

  mozart::CompositorPtr compositor =
      application_context_->ConnectToEnvironmentService<mozart::Compositor>();
  compositor.set_connection_error_handler([] {
    FTL_LOG(ERROR) << "Exiting due to compositor connection error.";
    exit(1);
  });

  registry_.reset(new ViewRegistry(std::move(compositor)));
  LaunchAssociates(params);

  application_context_->outgoing_services()->AddService<mozart::ViewManager>(
      [this](fidl::InterfaceRequest<mozart::ViewManager> request) {
        view_manager_bindings_.AddBinding(
            std::make_unique<ViewManagerImpl>(registry_.get()),
            std::move(request));
      });
}

ViewManagerApp::~ViewManagerApp() {}

void ViewManagerApp::LaunchAssociates(Params* params) {
  for (auto& launch_info : params->TakeAssociates()) {
    auto url = launch_info->url;
    app::ServiceProviderPtr services;
    app::ApplicationControllerPtr controller;

    launch_info->services = services.NewRequest();
    application_context_->launcher()->CreateApplication(
        std::move(launch_info), controller.NewRequest());
    auto view_associate =
        app::ConnectToService<mozart::ViewAssociate>(services.get());

    // Wire up the associate to the ViewManager.
    mozart::ViewAssociateOwnerPtr owner;
    registry_->RegisterViewAssociate(
        registry_.get(),
        mozart::ViewAssociatePtr::Create(std::move(view_associate)),
        owner.NewRequest(), url);
    owner.set_connection_error_handler(
        [url] { FTL_LOG(ERROR) << "View associate " << url << " died"; });
    view_associate_controllers_.push_back(std::move(controller));
    view_associate_owners_.push_back(std::move(owner));
  }
  registry_->FinishedRegisteringViewAssociates();
}

}  // namespace view_manager
