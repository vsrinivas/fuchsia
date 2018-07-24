// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/module_controller_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/time/time_delta.h>

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
    fuchsia::sys::Launcher* const launcher,
    fuchsia::modular::AppConfig module_config,
    const fuchsia::modular::ModuleData* const module_data,
    fuchsia::sys::ServiceListPtr service_list,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewProvider>
        view_provider_request)
    : story_controller_impl_(story_controller_impl),
      app_client_(
          launcher, CloneStruct(module_config),
          std::string(kAppStoragePath) + HashModuleUrl(module_config.url),
          std::move(service_list)),
      module_data_(module_data) {
  app_client_.SetAppErrorHandler([this] { OnAppConnectionError(); });
  app_client_.services().ConnectToService(std::move(view_provider_request));
}

ModuleControllerImpl::~ModuleControllerImpl() {}

void ModuleControllerImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::ModuleController> request) {
  module_controller_bindings_.AddBinding(this, std::move(request));
  // Notify of initial state on connection.
  NotifyStateChange();
}

// If the ComponentController connection closes, it means the module cannot be
// started. We indicate this by the ERROR state.
void ModuleControllerImpl::OnAppConnectionError() {
  SetState(fuchsia::modular::ModuleState::ERROR);
}

void ModuleControllerImpl::SetState(
    const fuchsia::modular::ModuleState new_state) {
  if (state_ == new_state) {
    return;
  }

  state_ = new_state;
  NotifyStateChange();
}

void ModuleControllerImpl::Teardown(std::function<void()> done) {
  teardown_done_callbacks_.push_back(done);

  if (teardown_done_callbacks_.size() != 1) {
    // Not the first request, Stop() in progress.
    return;
  }

  auto cont = [this] {
    SetState(fuchsia::modular::ModuleState::STOPPED);

    // We take ownership of *this from |story_controller_impl_| so that
    // teardown happens in StoryControllerImpl but *this is still alive when we
    // call |teardown_done_callbacks_|. One or more of the callbacks may be a
    // result callback for fuchsia::modular::ModuleController::Stop() and since
    // *this owns the fidl::Binding for the channel on which the result message
    // will be sent, it must be alive when the message is posted.
    // TODO(thatguy,mesch): This point is reachable from two distinct
    // code-paths: originating from ModuleControllerImpl::Stop() or
    // StoryControllerImpl::Stop(). It is not clear whether ReleaseModule()
    // must be called *before* these done callbacks are called, or whether we
    // can move this call below the loop and have ReleaseModule also delete
    // *this.
    story_controller_impl_->ReleaseModule(this);

    for (auto& done : teardown_done_callbacks_) {
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

  // Tear down the module application through the normal procedure with timeout.
  app_client_.Teardown(kBasicTimeout, cont);
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

void ModuleControllerImpl::NotifyStateChange() {
  for (auto& binding : module_controller_bindings_.bindings()) {
    binding->events().OnStateChange(state_);
  }
}

}  // namespace modular
