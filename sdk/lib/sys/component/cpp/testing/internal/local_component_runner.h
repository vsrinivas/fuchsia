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
#include <lib/fit/function.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <map>
#include <memory>

namespace component_testing {
namespace internal {

class LocalComponentRunner;

class LocalComponentInstance final : public fuchsia::component::runner::ComponentController {
 public:
  // Constructed by the LocalComponentRunner when the runner receives a request
  // to start a local component. The runner may optionally pass an on_exit
  // callback. This callback is not set for `LocalComponent*` component types
  // because they must stay alive for the lifetime of the Realm.
  explicit LocalComponentInstance(
      fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller,
      async_dispatcher_t* dispatcher,
      fit::function<void(LocalComponentInstance*, std::unique_ptr<LocalComponentHandles>)> on_start,
      fit::closure on_exit);

  LocalComponentInstance(LocalComponentInstance&& other) = delete;
  LocalComponentInstance& operator=(LocalComponentInstance&& other) = delete;

  LocalComponentInstance(const LocalComponentInstance& other) = delete;
  LocalComponentInstance& operator=(const LocalComponentInstance& other) = delete;

  // Called after constructing the |LocalComponentInstance|, to start the
  // component.
  void Start(std::unique_ptr<LocalComponentHandles> handles);

  // Saves a closure to be called if component manager calls
  // |ComponentController::Stop()|. This closure should not be set for
  // |LocalComponent|s added by raw pointer because the Realm does not control
  // the lifecycle of the |LocalComponent| and the pointer could be invalid.
  void SetOnStop(fit::closure on_stop);

  // Returns true after Start() and before Exit().
  bool IsRunning();

 private:
  // fuchsia::component::runner::ComponentController
  void Stop() override;

  // fuchsia::component::runner::ComponentController
  void Kill() override;

  // If on_exit is set, close the ComponentController and call the given on_exit
  // function.
  void Exit(zx_status_t);

  fidl::Binding<fuchsia::component::runner::ComponentController> binding_;

  // If the component calls `handles->Exit()` during `LocalComponent::Start()`,
  // the LocalComponentInstance will _not_ immediately call `Exit()`. It will
  // save the provided status, and call `Exit()` after the component has
  // `started_`.
  cpp17::optional<zx_status_t> pending_exit_status_;

  // Set to true at the beginning of `Start()`, and false at the completion
  // of `Start()`.
  bool starting_;

  // Set to true at the completion of `Start()`.
  bool started_;

  // Called when starting the component.
  fit::function<void(LocalComponentInstance*, std::unique_ptr<LocalComponentHandles>)> on_start_;

  // Called when the component is exiting, purposefully or as a result of a
  // ComponentController::Kill().
  fit::closure on_exit_;

  // Called when the ComponentController::Stop() method is called.
  fit::closure on_stop_;
};

using LocalComponents = std::map<std::string, LocalComponentImpl>;
using LocalComponentInstances = std::map<std::string, std::unique_ptr<LocalComponentInstance>>;

class LocalComponentRunner final : fuchsia::component::runner::ComponentRunner {
 public:
  LocalComponentRunner(LocalComponents components, async_dispatcher_t* dispatcher);

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
  // Returns true if the runner has a component with the given name that is
  // ready to be started (either it has not started, or it has stopped and
  // can be started again).
  bool ContainsReadyComponent(std::string name) const;

  // The list of components that are not running but can be started.
  LocalComponents ready_components_;

  // ComponentInstance objects for components that have been started.
  LocalComponentInstances running_components_;
  fidl::Binding<fuchsia::component::runner::ComponentRunner> binding_;
  async_dispatcher_t* dispatcher_;
};

class LocalComponentRunner::Builder final {
 public:
  Builder() = default;

  Builder(LocalComponentRunner::Builder&& other) = default;
  Builder& operator=(LocalComponentRunner::Builder&& other) = default;

  Builder(const LocalComponentRunner::Builder& other) = delete;
  Builder& operator=(const LocalComponentRunner::Builder& other) = delete;

  std::unique_ptr<LocalComponentRunner> Build(async_dispatcher_t* dispatcher);

  void Register(std::string name, LocalComponentImpl mock);

 private:
  bool Contains(std::string name) const;

  LocalComponents components_;
};

}  // namespace internal
}  // namespace component_testing

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_LOCAL_COMPONENT_RUNNER_H_
