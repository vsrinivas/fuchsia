// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/module_controller_impl.h"

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "peridot/bin/story_runner/story_controller_impl.h"
#include "peridot/lib/common/teardown.h"

namespace modular {

constexpr char kAppStoragePath[] = "/data/APP_DATA";

namespace {

// A stopgap solution to map a module's url to a directory name where the
// module's /data is mapped. We need three properties here - (1) two module urls
// that are the same get mapped to the same hash, (2) two modules urls that are
// different don't get the same name (with very high probability) and (3) the
// name is visually inspectable.
std::string HashModuleUrl(const std::string& module_url) {
  std::size_t found = module_url.find_last_of('/');
  auto last_part =
      found == module_url.length() - 1 ? "" : module_url.substr(found + 1);
  return std::to_string(std::hash<std::string>{}(module_url)) + last_part;
}

};  // namespace

ModuleControllerImpl::ModuleControllerImpl(
    StoryControllerImpl* const story_controller_impl,
    app::ApplicationLauncher* const application_launcher,
    AppConfigPtr module_config,
    const fidl::Array<fidl::String>& module_path,
    fidl::InterfaceHandle<ModuleContext> module_context,
    fidl::InterfaceRequest<mozart::ViewProvider> view_provider_request,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services)
    : story_controller_impl_(story_controller_impl),
      app_client_(
          application_launcher,
          module_config.Clone(),
          std::string(kAppStoragePath) + HashModuleUrl(module_config->url)),
      module_path_(module_path.Clone()) {
  app_client_.SetAppErrorHandler([this] { SetState(ModuleState::ERROR); });

  ConnectToService(app_client_.services(), module_service_.NewRequest());
  module_service_.set_connection_error_handler([this] { OnConnectionError(); });
  module_service_->Initialize(std::move(module_context),
                              std::move(outgoing_services),
                              std::move(incoming_services));

  ConnectToService(app_client_.services(), std::move(view_provider_request));
}

ModuleControllerImpl::~ModuleControllerImpl() {}

void ModuleControllerImpl::Connect(
    fidl::InterfaceRequest<ModuleController> request) {
  bindings_.AddBinding(this, std::move(request));
}

// If the Module instance closes its own connection, we signal this to
// all current and future watchers by an appropriate state transition.
void ModuleControllerImpl::OnConnectionError() {
  if (state_ == ModuleState::STARTING) {
    SetState(ModuleState::UNLINKED);
  } else {
    SetState(ModuleState::ERROR);
  }
}

void ModuleControllerImpl::SetState(const ModuleState new_state) {
  if (state_ == new_state) {
    return;
  }

  state_ = new_state;
  watchers_.ForAllPtrs(
      [this](ModuleWatcher* const watcher) { watcher->OnStateChange(state_); });
}

void ModuleControllerImpl::Teardown(std::function<void()> done) {
  teardown_.push_back(done);

  if (teardown_.size() != 1) {
    // Not the first request, Stop() in progress.
    return;
  }

  auto cont = [this] {
    module_service_.reset();
    SetState(ModuleState::STOPPED);

    // ReleaseModule() must be called before the callbacks, because
    // StoryControllerImpl::Stop() relies on being called back *after* the
    // module controller was disposed.
    story_controller_impl_->ReleaseModule(this);

    for (auto& done : teardown_) {
      done();
    }

    // |this| must be deleted after the callbacks so that the |done()| calls
    // above can be dispatched while the bindings still exist in case they are
    // FIDL method callbacks.
    //
    // The destructor of |this| deletes |app_client_|, which will kill the
    // related application if it's still running.
    delete this;
  };

  // At this point, it's no longer an error if the module closes its
  // connection, or the application exits.
  app_client_.SetAppErrorHandler(nullptr);

  // If the module was UNLINKED, stop it without a delay. Otherwise
  // call Module.Stop(), but also schedule a timeout in case it
  // doesn't return from Stop().
  if (state_ == ModuleState::UNLINKED) {
    module_service_.set_connection_error_handler(nullptr);
    fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(cont);
  } else {
    app_client_.Teardown(kBasicTimeout, cont);
  }
}

void ModuleControllerImpl::Watch(fidl::InterfaceHandle<ModuleWatcher> watcher) {
  auto ptr = fidl::InterfacePtr<ModuleWatcher>::Create(std::move(watcher));
  ptr->OnStateChange(state_);
  watchers_.AddInterfacePtr(std::move(ptr));
}

void ModuleControllerImpl::Focus() {
  story_controller_impl_->FocusModule(module_path_);
}

void ModuleControllerImpl::Defocus() {
  story_controller_impl_->DefocusModule(module_path_);
}

void ModuleControllerImpl::Stop(const StopCallback& done) {
  story_controller_impl_->StopModule(module_path_, done);
}

}  // namespace modular
