// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular/testing/cpp/fake_component.h>

namespace modular_testing {
namespace {
constexpr char kServiceRootPath[] = "/svc";

std::unique_ptr<sys::ComponentContext> CreateComponentContext(
    fuchsia::sys::StartupInfo* startup_info, async_dispatcher_t* dispatcher) {
  fuchsia::sys::FlatNamespace& flat = startup_info->flat_namespace;
  if (flat.paths.size() != flat.directories.size()) {
    return nullptr;
  }

  zx::channel service_root;
  for (size_t i = 0; i < flat.paths.size(); ++i) {
    if (flat.paths.at(i) == kServiceRootPath) {
      service_root = std::move(flat.directories.at(i));
      break;
    }
  }

  return std::make_unique<sys::ComponentContext>(
      std::make_unique<sys::ServiceDirectory>(std::move(service_root)),
      std::move(startup_info->launch_info.directory_request), dispatcher);
}
}  // namespace

FakeComponent::FakeComponent(Args args) : args_(std::move(args)) {}
FakeComponent::~FakeComponent() = default;

modular_testing::TestHarnessBuilder::InterceptOptions FakeComponent::BuildInterceptOptions(
    async_dispatcher_t* dispatcher) {
  modular_testing::TestHarnessBuilder::InterceptOptions options;
  options.url = args_.url;
  options.sandbox_services = args_.sandbox_services;
  options.launch_handler =
      [this, dispatcher](fuchsia::sys::StartupInfo startup_info,
                         fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
                             intercepted_component) {
        intercepted_component_ptr_.Bind(std::move(intercepted_component), dispatcher);
        intercepted_component_ptr_.events().OnKill = [this] {
          component_context_.reset();
          OnDestroy();
        };

        component_context_ = CreateComponentContext(&startup_info, dispatcher);
        component_context_->outgoing()->AddPublicService(
            lifecycle_bindings_.GetHandler(this, dispatcher));

        OnCreate(std::move(startup_info));
      };

  return options;
}

std::string FakeComponent::url() const { return args_.url; }
bool FakeComponent::is_running() const { return !!component_context_; }

sys::ComponentContext* FakeComponent::component_context() { return component_context_.get(); }
const sys::ComponentContext* FakeComponent::component_context() const {
  return component_context_.get();
}

void FakeComponent::Exit(int64_t exit_code, fuchsia::sys::TerminationReason reason) {
  if (intercepted_component_ptr_) {
    intercepted_component_ptr_->Exit(exit_code, reason);
  }
}

void FakeComponent::Terminate() { Exit(0); }

}  // namespace modular_testing
