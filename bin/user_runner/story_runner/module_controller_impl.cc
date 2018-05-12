// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/module_controller_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"
#include "peridot/bin/user_runner/story_runner/story_controller_impl.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/clone.h"

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
    component::ApplicationLauncher* const application_launcher,
    AppConfig module_config,
    const ModuleData* const module_data,
    component::ServiceListPtr service_list,
    fidl::InterfaceHandle<ModuleContext> module_context,
    fidl::InterfaceRequest<views_v1::ViewProvider> view_provider_request,
    fidl::InterfaceRequest<component::ServiceProvider> incoming_services)
    : story_controller_impl_(story_controller_impl),
      app_client_(
          application_launcher,
          CloneStruct(module_config),
          std::string(kAppStoragePath) + HashModuleUrl(module_config.url),
          std::move(service_list)),
      module_data_(module_data) {
  app_client_.SetAppErrorHandler([this] { OnAppConnectionError(); });

  app_client_.services().ConnectToService(module_service_.NewRequest());

  module_service_.set_error_handler([this] { OnModuleConnectionError(); });

  module_service_->Initialize(std::move(module_context),
                              std::move(incoming_services));

  app_client_.services().ConnectToService(std::move(view_provider_request));

  // Push the initial module state to story controller. TODO(mesch): This is
  // only needed for the root module to transition the story state to STARTING
  // and get IsRunning() to true. This could be handled inside
  // StoryControllerImpl too.
  story_controller_impl_->OnModuleStateChange(module_data_->module_path,
                                              state_);
}

ModuleControllerImpl::~ModuleControllerImpl() {}

void ModuleControllerImpl::Connect(
    fidl::InterfaceRequest<ModuleController> request) {
  module_controller_bindings_.AddBinding(this, std::move(request));
}

// If the ApplicationController connection closes, it means the module cannot be
// started. We indicate this by the ERROR state.
void ModuleControllerImpl::OnAppConnectionError() {
  SetState(ModuleState::ERROR);
}

// If the Module connection closes, it means the module doesn't implement the
// Module service and doesn't fully participate in the story, especially it
// doesn't have access to links. We indicate this by the UNLINKED state.
//
// If the module cannot run at all, it also cannot keep its Module connection
// open, but that's already covered by the ERROR state, cf. above.
void ModuleControllerImpl::OnModuleConnectionError() {
  if (state_ != ModuleState::ERROR) {
    SetState(ModuleState::UNLINKED);
  }
}

void ModuleControllerImpl::SetState(const ModuleState new_state) {
  if (state_ == new_state) {
    return;
  }

  state_ = new_state;
  for (const auto& i : watchers_.ptrs()) {
    (*i)->OnStateChange(state_);
  }

  story_controller_impl_->OnModuleStateChange(module_data_->module_path,
                                              state_);
}

void ModuleControllerImpl::Teardown(std::function<void()> done) {
  teardown_.push_back(done);

  if (teardown_.size() != 1) {
    // Not the first request, Stop() in progress.
    return;
  }

  auto cont = [this] {
    module_service_.Unbind();
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
  module_service_.set_error_handler(nullptr);

  // If the module was UNLINKED, stop it without a delay. Otherwise
  // call Module.Stop(), but also schedule a timeout in case it
  // doesn't return from Stop().
  if (state_ == ModuleState::UNLINKED) {
    async::PostTask(async_get_default(), cont);
  } else {
    app_client_.Teardown(kBasicTimeout, cont);
  }
}

void ModuleControllerImpl::Watch(fidl::InterfaceHandle<ModuleWatcher> watcher) {
  auto ptr = watcher.Bind();
  ptr->OnStateChange(state_);
  watchers_.AddInterfacePtr(std::move(ptr));
}

void ModuleControllerImpl::Focus() {
  story_controller_impl_->FocusModule(module_data_->module_path);
}

void ModuleControllerImpl::Defocus() {
  story_controller_impl_->DefocusModule(module_data_->module_path);
}

void ModuleControllerImpl::Stop(StopCallback done) {
  story_controller_impl_->StopModule(module_data_->module_path, done);
}

}  // namespace modular
