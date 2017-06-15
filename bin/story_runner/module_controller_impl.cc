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

ModuleControllerImpl::ModuleControllerImpl(
    StoryControllerImpl* const story_controller_impl,
    app::ApplicationControllerPtr module_application,
    ModulePtr module,
    const fidl::Array<fidl::String>& module_path)
    : story_controller_impl_(story_controller_impl),
      module_application_(std::move(module_application)),
      module_(std::move(module)),
      module_path_(module_path.Clone()) {
  module_application_.set_connection_error_handler(
      [this] { SetState(ModuleState::ERROR); });
  module_.set_connection_error_handler([this] { OnConnectionError(); });
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

    module_.reset();
    SetState(ModuleState::STOPPED);

    // ReleaseModule() must be called before the callbacks, because
    // StoryControllerImpl::Stop() relies on being called back *after* the
    // module controller was disposed.
    story_controller_impl_->ReleaseModule(this);

    for (auto& done : teardown_) {
      done();
    }

    // |this| must be deleted after the callbacks, because otherwise
    // the callback for ModuleController::Stop() cannot be invoked
    // anymore.
    delete this;
  };

  // At this point, it's no longer an error if the module closes its
  // connection, or the application exits.
  module_application_.set_connection_error_handler(nullptr);
  module_.set_connection_error_handler(nullptr);

  // If the module was UNLINKED, stop it without a delay. Otherwise
  // call Module.Stop(), but also schedule a timeout in case it
  // doesn't return from Stop().
  if (state_ == ModuleState::UNLINKED) {
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(cont);

  } else {
    module_->Stop(cont);
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
  Teardown(done);
}

}  // namespace modular
