// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular_test_harness/cpp/fake_component.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>

namespace modular {
namespace testing {
namespace {
constexpr char kServiceRootPath[] = "/svc";

std::unique_ptr<sys::ComponentContext> CreateComponentContext(
    fuchsia::sys::StartupInfo* startup_info) {
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
      std::move(startup_info->launch_info.directory_request));
}
}  // namespace

FakeComponent::~FakeComponent() = default;

TestHarnessBuilder::OnNewComponentHandler FakeComponent::GetOnCreateHandler() {
  return
      [this](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              intercepted_component) {
        intercepted_component_ptr_ = intercepted_component.Bind();
        intercepted_component_ptr_.events().OnKill = [this] {
          component_context_.reset();
          OnDestroy();
        };

        component_context_ = CreateComponentContext(&startup_info);
        component_context_->outgoing()->AddPublicService(
            lifecycle_bindings_.GetHandler(this));

        OnCreate(std::move(startup_info));
      };
}

bool FakeComponent::is_running() const { return !!component_context_; }

sys::ComponentContext* FakeComponent::component_context() {
  ZX_ASSERT(is_running());
  return component_context_.get();
}

void FakeComponent::Exit(int64_t exit_code,
                         fuchsia::sys::TerminationReason reason) {
  ZX_ASSERT(is_running());
  intercepted_component_ptr_->Exit(exit_code, reason);
}

void FakeComponent::Terminate() {
  Exit(0);
}

}  // namespace testing
}  // namespace modular
