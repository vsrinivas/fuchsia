// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/module_controller_impl.h"

#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

constexpr ftl::TimeDelta kStoryTearDownTimeout = ftl::TimeDelta::FromSeconds(1);

ModuleControllerImpl::ModuleControllerImpl(
    StoryImpl* const story_impl,
    const fidl::String& url,
    fidl::InterfacePtr<Module> module,
    fidl::InterfaceRequest<ModuleController> module_controller)
    : story_impl_(story_impl),
      url_(url),
      module_(std::move(module)),
      binding_(this, std::move(module_controller)) {
  // If the Module instance closes its own connection, we signal this
  // as error to all current and future watchers.
  module_.set_connection_error_handler([this]() {
    SetState(ModuleState::ERROR);
  });
}

void ModuleControllerImpl::SetState(const ModuleState new_state) {
  if (state_ == new_state) {
    return;
  }

  state_ = new_state;
  for (auto& watcher : watchers_) {
    watcher->OnStateChange(state_);
  }
}

void ModuleControllerImpl::TearDown(std::function<void()> done) {
  teardown_.push_back(done);

  if (teardown_.size() != 1) {
    // Not the first request, Stop() in progress.
    return;
  }

  // This function causes this to be deleted when called once, but may
  // be called twice, so the second call must be protected from fully
  // executing.
  auto called = std::make_shared<bool>(false);
  auto cont = [this, called]() {
    if (*called) {
      return;
    }
    *called = true;

    module_.reset();

    SetState(ModuleState::STOPPED);

    // Value of teardown must survive deletion of this.
    auto teardown = teardown_;

    story_impl_->DisposeModule(this);

    for (auto& done : teardown) {
      done();
    }
  };

  // At this point, it's no longer an error if the module closes its
  // connection.
  module_.set_connection_error_handler(nullptr);

  // Call Module.Stop(), but also schedule a timeout.
  module_->Stop(cont);

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      cont, kStoryTearDownTimeout);
}

void ModuleControllerImpl::Watch(fidl::InterfaceHandle<ModuleWatcher> watcher) {
  watchers_.push_back(
      fidl::InterfacePtr<ModuleWatcher>::Create(std::move(watcher)));
  watchers_.back()->OnStateChange(state_);
}

void ModuleControllerImpl::Stop(const StopCallback& done) {
  TearDown(done);
}

}  // namespace modular
