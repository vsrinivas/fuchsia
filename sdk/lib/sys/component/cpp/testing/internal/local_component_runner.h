// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_LOCAL_COMPONENT_RUNNER_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_LOCAL_COMPONENT_RUNNER_H_

#include <fuchsia/component/runner/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <map>
#include <memory>

namespace component_testing {
namespace internal {

class ComponentController final : public fuchsia::component::runner::ComponentController {
 public:
  ComponentController();

  void Stop() override;
  void Kill() override;

  void Bind(
      fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller_request,
      async_dispatcher_t* dispatcher) {
    binding_.Bind(std::move(controller_request), dispatcher);
  }

 private:
  fidl::Binding<fuchsia::component::runner::ComponentController> binding_;
};

class RegisteredComponent final {
 public:
  RegisteredComponent() = default;
  explicit RegisteredComponent(LocalComponent* component);

  RegisteredComponent(RegisteredComponent&& other) = default;
  RegisteredComponent& operator=(RegisteredComponent&& other) = default;

  RegisteredComponent(const RegisteredComponent& other) = delete;
  RegisteredComponent& operator=(const RegisteredComponent& other) = delete;

  void Start(std::unique_ptr<LocalComponentHandles> handles,
             fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller,
             async_dispatcher_t* dispatcher);

 private:
  LocalComponent* component_ = nullptr;
  // Controller needs to live in the heap because it can't be moved nor copied.
  std::unique_ptr<ComponentController> controller_ = nullptr;
};

using LocalComponentRegistry = std::map<std::string, std::shared_ptr<RegisteredComponent>>;

class LocalComponentRunner final : fuchsia::component::runner::ComponentRunner {
 public:
  LocalComponentRunner(LocalComponentRegistry components, async_dispatcher_t* dispatcher);

  LocalComponentRunner(LocalComponentRunner&& other) = delete;
  LocalComponentRunner& operator=(LocalComponentRunner&& other) = delete;

  LocalComponentRunner(const LocalComponentRunner& other) = delete;
  LocalComponentRunner& operator=(const LocalComponentRunner& other) = delete;

  fidl::InterfaceHandle<fuchsia::component::runner::ComponentRunner> NewBinding();

  void Start(
      fuchsia::component::runner::ComponentStartInfo start_info,
      fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller) override;

  class Builder;

 private:
  bool Contains(std::string name) const;

  LocalComponentRegistry components_;
  async_dispatcher_t* dispatcher_ = nullptr;
  fidl::Binding<fuchsia::component::runner::ComponentRunner> binding_;
};

class LocalComponentRunner::Builder final {
 public:
  Builder() = default;

  std::unique_ptr<LocalComponentRunner> Build(async_dispatcher_t* dispatcher);

  void Register(std::string name, LocalComponent* mock);

 private:
  bool Contains(std::string name) const;

  LocalComponentRegistry components_;
};

}  // namespace internal
}  // namespace component_testing

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_LOCAL_COMPONENT_RUNNER_H_
