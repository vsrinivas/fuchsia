// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/module_controller_impl.h"

#include "apps/modular/src/story_runner/story_controller_impl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

constexpr ftl::TimeDelta kStoryTeardownTimeout = ftl::TimeDelta::FromSeconds(1);

// Template specializations for fidl services that don't have a Terminate()
template <>
void AppClient<Module>::ServiceTerminate(const std::function<void()>& done) {
  FTL_NOTREACHED();
}

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
      app_client_(application_launcher, std::move(module_config)),
      module_path_(module_path.Clone()) {
  app_client_.SetAppErrorHandler([this] { SetState(ModuleState::ERROR); });
  app_client_.primary_service().set_connection_error_handler(
      [this] { OnConnectionError(); });
  app_client_.primary_service()->Initialize(std::move(module_context),
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

  // This function causes this to be deleted when called once, but may
  // be called twice, so the second call must be protected from fully
  // executing.
  auto called = std::make_shared<bool>(false);
  auto cont = [this, called] {
    if (*called) {
      return;
    }
    *called = true;

    app_client_.primary_service().reset();
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
    // Destructing |this| will delete |app_client_|, which will kill the
    // related application if it's still running.
    // TODO(jimbe) This line needs review if (someday) we add support in
    // Modular for multi-tenancy of Modules in ELF executables.
    delete this;
  };

  // At this point, it's no longer an error if the module closes its
  // connection, or the application exits.
  app_client_.SetAppErrorHandler(nullptr);

  // If the module was UNLINKED, stop it without a delay. Otherwise
  // call Module.Stop(), but also schedule a timeout in case it
  // doesn't return from Stop().
  if (state_ == ModuleState::UNLINKED) {
    app_client_.primary_service().set_connection_error_handler(nullptr);
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(cont);
  } else {
    // The contract for Stop() is that the Application will be killed when
    // the Module's handle is closed.
    app_client_.primary_service().set_connection_error_handler(cont);

    // TODO(jimbe) Remove the lambda parameter to Stop(), which is no
    // longer used. [FW-265] Expected to happen as part of implementing the
    // Lifecycle interface for namespaces.
    app_client_.primary_service()->Stop([] {});
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        cont, kStoryTeardownTimeout);
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
