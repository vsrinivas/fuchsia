// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/mock_runner/mock_runner.h"

#include <memory>

#include "lib/component/cpp/environment_services.h"
#include "lib/fxl/logging.h"

namespace component {
namespace testing {

FakeSubComponent::FakeSubComponent(
    uint64_t id, fuchsia::sys::Package application,
    fuchsia::sys::StartupInfo startup_info,
    ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
    MockRunner* runner)
    : id_(id),
      return_code_(0),
      alive_(true),
      binding_(this),
      runner_(runner),
      startup_context_(StartupContext::CreateFrom(std::move(startup_info))) {
  if (controller.is_valid()) {
    binding_.Bind(std::move(controller));
    binding_.set_error_handler([this] { Kill(); });
  }
}

FakeSubComponent::~FakeSubComponent() { Kill(); }

void FakeSubComponent::Kill() {
  if (!alive_) {
    return;
  }
  alive_ = false;
  SendReturnCodeIfTerminated();
  // this should kill the object
  runner_->ExtractComponent(id_);
}

void FakeSubComponent::SendReturnCodeIfTerminated() {
  if (!alive_) {
    for (const auto& iter : wait_callbacks_) {
      iter(return_code_);
    }
    wait_callbacks_.clear();
    binding_.events().OnTerminated(return_code_, TerminationReason::EXITED);
  }
}

void FakeSubComponent::Detach() { binding_.set_error_handler(nullptr); }

MockRunner::MockRunner()
    : loop_(&kAsyncLoopConfigAttachToThread),
      context_(component::StartupContext::CreateFromStartupInfo()),
      mock_binding_(this),
      component_id_counter_(0) {
  context_->outgoing().AddPublicService(bindings_.GetHandler(this));
  mockrunner::MockRunnerPtr mock_runner;
  mock_binding_.Bind(mock_runner.NewRequest());
  mockrunner::MockRunnerRegistryPtr runner_registry_ptr;
  component::ConnectToEnvironmentService(runner_registry_ptr.NewRequest());
  runner_registry_ptr->Register(std::move(mock_runner));
}

MockRunner::~MockRunner() = default;

void MockRunner::Crash() { exit(1); }

void MockRunner::ConnectToComponent(
    uint64_t id, ::fidl::InterfaceRequest<mockrunner::MockComponent> req) {
  auto it = components_.find(id);
  if (it == components_.end()) {
    return;
  }
  auto component = it->second.get();
  component->AddMockControllerBinding(std::move(req));
}

void MockRunner::StartComponent(
    fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
    ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  auto id = component_id_counter_++;

  mockrunner::ComponentInfo info{.unique_id = id,
                                 .url = startup_info.launch_info.url};
  auto fake_component = std::make_unique<FakeSubComponent>(
      id, std::move(application), std::move(startup_info),
      std::move(controller), this);

  mock_binding_.events().OnComponentCreated(std::move(info));
  components_.insert({id, std::move(fake_component)});
}

std::unique_ptr<FakeSubComponent> MockRunner::ExtractComponent(uint64_t id) {
  auto it = components_.find(id);
  if (it == components_.end()) {
    return nullptr;
  }
  auto component = std::move(it->second);

  components_.erase(it);

  // send event
  mock_binding_.events().OnComponentKilled(id);

  return component;
}

}  // namespace testing
}  // namespace component
