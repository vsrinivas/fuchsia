// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/runner/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/data/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/component/cpp/testing/internal/errors.h>
#include <lib/sys/component/cpp/testing/internal/local_component_runner.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <cstddef>
#include <memory>
#include <optional>

namespace component_testing {
namespace internal {

namespace {

std::string ExtractLocalComponentName(const fuchsia::data::Dictionary& program) {
  ZX_ASSERT_MSG(program.has_entries(), "Received empty program from Component Manager");
  for (const auto& entry : program.entries()) {
    if (entry.key == fuchsia::component::test::LOCAL_COMPONENT_NAME_KEY) {
      ZX_ASSERT_MSG(entry.value->is_str(), "Received local component key of wrong type");
      return entry.value->str();
    }
  }

  ZX_PANIC("Received program without local component key");
}

std::unique_ptr<LocalComponentHandles> CreateFromStartInfo(
    fuchsia::component::runner::ComponentStartInfo start_info, async_dispatcher_t* dispatcher) {
  fdio_ns_t* ns;
  ZX_COMPONENT_ASSERT_STATUS_OK("CreateFromStartInfo", fdio_ns_create(&ns));
  for (auto& entry : *start_info.mutable_ns()) {
    ZX_COMPONENT_ASSERT_STATUS_OK(
        "CreateFromStartInfo",
        fdio_ns_bind(ns, entry.path().c_str(), entry.mutable_directory()->TakeChannel().release()));
  }

  sys::OutgoingDirectory outgoing_dir;
  outgoing_dir.Serve(start_info.mutable_outgoing_dir()->TakeChannel(), dispatcher);
  return std::make_unique<LocalComponentHandles>(ns, std::move(outgoing_dir));
}

}  // namespace

ComponentController::ComponentController() : binding_(this) {}

void ComponentController::Stop() {}
void ComponentController::Kill() {}

RegisteredComponent::RegisteredComponent(LocalComponent* component)
    : component_(component), controller_(std::make_unique<ComponentController>()) {}

// TODO(fxbug.dev/89831): Implement fuchsia.component.runner/ComponentController.
void RegisteredComponent::Start(
    std::unique_ptr<LocalComponentHandles> handles,
    fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller,
    async_dispatcher_t* dispatcher) {
  controller_->Bind(std::move(controller), dispatcher);
  component_->Start(std::move(handles));
}

LocalComponentRunner::LocalComponentRunner(LocalComponentRegistry components,
                                           async_dispatcher_t* dispatcher)
    : components_(std::move(components)), dispatcher_(dispatcher), binding_(this) {}

fidl::InterfaceHandle<fuchsia::component::runner::ComponentRunner>
LocalComponentRunner::NewBinding() {
  return binding_.NewBinding(dispatcher_);
}

void LocalComponentRunner::Start(
    fuchsia::component::runner::ComponentStartInfo start_info,
    fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller) {
  ZX_ASSERT_MSG(start_info.has_program(), "Component manager sent start_info without program");
  std::string name = ExtractLocalComponentName(start_info.program());
  ZX_ASSERT_MSG(Contains(name), "Component manager sent unregistered local component name id: %s",
                name.c_str());
  auto& component = components_[name];
  component->Start(CreateFromStartInfo(std::move(start_info), dispatcher_), std::move(controller),
                   dispatcher_);
}

bool LocalComponentRunner::Contains(std::string name) const {
  return components_.find(name) != components_.cend();
}

std::unique_ptr<LocalComponentRunner> LocalComponentRunner::Builder::Build(
    async_dispatcher_t* dispatcher) {
  return std::make_unique<LocalComponentRunner>(components_, dispatcher);
}

void LocalComponentRunner::Builder::Register(std::string name, LocalComponent* mock) {
  ZX_ASSERT_MSG(!Contains(name), "Local component with same name being added: %s", name.c_str());
  components_[name] = std::make_shared<RegisteredComponent>(mock);
}

bool LocalComponentRunner::Builder::Contains(std::string name) const {
  return components_.find(name) != components_.cend();
}

}  // namespace internal
}  // namespace component_testing
