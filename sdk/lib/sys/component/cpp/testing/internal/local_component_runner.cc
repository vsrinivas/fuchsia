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
#include <iterator>
#include <memory>
#include <optional>
#include <variant>

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

std::unique_ptr<LocalComponentHandles> CreateHandlesFromStartInfo(
    fuchsia::component::runner::ComponentStartInfo start_info, async_dispatcher_t* dispatcher) {
  fdio_ns_t* ns;
  ZX_COMPONENT_ASSERT_STATUS_OK("CreateHandlesFromStartInfo", fdio_ns_create(&ns));
  for (auto& entry : *start_info.mutable_ns()) {
    ZX_COMPONENT_ASSERT_STATUS_OK(
        "CreateHandlesFromStartInfo",
        fdio_ns_bind(ns, entry.path().c_str(), entry.mutable_directory()->TakeChannel().release()));
  }

  sys::OutgoingDirectory outgoing_dir;
  outgoing_dir.Serve(
#if __Fuchsia_API_level__ < 10
      start_info.mutable_outgoing_dir()->TakeChannel()
#else
      std::move(*start_info.mutable_outgoing_dir())
#endif
          ,
      dispatcher);
  return std::make_unique<LocalComponentHandles>(ns, std::move(outgoing_dir));
}

}  // namespace

LocalComponentInstance::LocalComponentInstance(
    fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller,
    async_dispatcher_t* dispatcher,
    fit::function<void(LocalComponentInstance*, std::unique_ptr<LocalComponentHandles>)> on_start,
    fit::closure on_exit)
    : binding_(this),
      starting_(false),
      started_(false),
      on_start_(std::move(on_start)),
      on_exit_(std::move(on_exit)) {
  ZX_COMPONENT_ASSERT_STATUS_OK("Bind ComponentController",
                                binding_.Bind(std::move(controller), dispatcher));
}

void LocalComponentInstance::SetOnStop(fit::closure on_stop) { on_stop_ = std::move(on_stop); }

void LocalComponentInstance::Start(std::unique_ptr<LocalComponentHandles> handles) {
  starting_ = true;
  handles->on_exit_ = [this](zx_status_t status) {
    if (started_) {
      Exit(status);
      // `this` may now be invalid
    } else {
      // Delay `Exit` (delay calling `on_exit_`, if set) until after `on_start_`
      // completes.
      pending_exit_status_ = status;
    }
  };
  // The `on_start_` callback calls LocalComponent->Start() or
  // LocalComponentImpl->OnStart() (among other things).
  on_start_(this, std::move(handles));
  if (pending_exit_status_) {
    // `ComponentInstance::Exit()` (which calls the
    // `ComponentInstance::on_exit_` callback) must not be called before the
    // `on_start_` callback completes.
    //
    // A LocalComponentImpl may call `Exit()` during the
    // `LocalComponentImpl::OnStart()` method. This is a legitimate use case, if
    // the component can complete its work via synchronous calls. See the
    // `RoutesProtocolToLocalComponentSync` test in `realm_builder_test.cc`, for
    // example. This test's component uses an `EchoSyncPtr` to invoke a client
    // request and get a response, before the `OnStart()` method completes.
    // Since the work is done, the client component is safe to terminate, by
    // calling `Exit()`.
    //
    // Note that calling `Exit()` before `on_start_` saves the provided status
    // (see above), but delays the call to `LocalComponentInstance::Exit()`
    // until here, after `on_start_` has completed.
    Exit(*pending_exit_status_);
    // `this` may now be invalid
  } else {
    started_ = true;
    starting_ = false;
  }
}

bool LocalComponentInstance::IsRunning() {
  return (starting_ || started_) && !pending_exit_status_ && binding_.is_bound();
}

void LocalComponentInstance::Stop() {
  if (on_stop_) {
    on_stop_();
    Exit(ZX_OK);
  }
  // The component should exit the loop, if any, or should have already
  // terminated. When it terminates, it should close the ComponentController.
  // If it doesn't, component manager will call Kill(), which can force close
  // the ComponentController.
}

void LocalComponentInstance::Kill() {
  // Close the ComponentController immediately.
  Exit(ZX_ERR_CANCELED);
}

void LocalComponentInstance::Exit(zx_status_t epitaph_value) {
  if (binding_.is_bound()) {
    binding_.Close(epitaph_value);
  }
  if (on_exit_) {
    // If on_exit is not set, this is a LocalComponent* type, which does not
    // support exiting. Don't close the binding while the component is still
    // running.
    auto on_exit = std::move(on_exit_);
    on_exit_ = nullptr;
    on_exit();
  }
}

LocalComponentRunner::LocalComponentRunner(LocalComponents components,
                                           async_dispatcher_t* dispatcher)
    : ready_components_(std::move(components)), binding_(this), dispatcher_(dispatcher) {}

fidl::InterfaceHandle<fuchsia::component::runner::ComponentRunner>
LocalComponentRunner::NewBinding() {
  return binding_.NewBinding(dispatcher_);
}

void LocalComponentRunner::Start(
    fuchsia::component::runner::ComponentStartInfo start_info,
    fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller) {
  ZX_ASSERT_MSG(start_info.has_program(), "Component manager sent start_info without program");
  std::string const name = ExtractLocalComponentName(start_info.program());
  ZX_ASSERT_MSG(ContainsReadyComponent(name),
                "Component manager requested a named LocalComponent that is unregistered, already "
                "running, or not restartable. Component name: %s",
                name.c_str());
  auto handles = CreateHandlesFromStartInfo(std::move(start_info), dispatcher_);
  // take the component from the ready_components_ list
  auto component = std::move(ready_components_[name]);
  ZX_ASSERT_MSG(ready_components_.erase(name) == 1, "ready component not erased");
  if (cpp17::holds_alternative<LocalComponent*>(component)) {
    auto local_component_ptr = cpp17::get<LocalComponent*>(component);
    auto on_start = [local_component_ptr](LocalComponentInstance*,
                                          std::unique_ptr<LocalComponentHandles> handles) {
      local_component_ptr->Start(std::move(handles));
      // Do not call instance->SetOnStop() for components added by
      // LocalComponent* (raw pointer). RealmBuilder does not manage the
      // lifecycle of these components, so the LocalComponent pointer may not
      // be valid, once started.
    };
    auto on_exit = [this, name]() mutable {
      // Drop the ComponentInstance. This also causes the ComponentController to
      // be dropped.
      ZX_ASSERT_MSG(running_components_.erase(name) == 1, "running component not erased");
      // Components added by LocalComponent* are not restartable, so they will
      // not be added back to the ready_components_.
    };
    running_components_[name] = std::make_unique<LocalComponentInstance>(
        std::move(controller), dispatcher_, std::move(on_start), std::move(on_exit));
  } else {
    auto local_component_factory = std::move(cpp17::get<LocalComponentFactory>(component));
    auto local_component = local_component_factory();
    auto on_start = [name, local_component = std::move(local_component)](
                        LocalComponentInstance* instance,
                        std::unique_ptr<LocalComponentHandles> handles) mutable {
      local_component->handles_ = std::move(handles);
      local_component->OnStart();
      // Drop the `LocalComponentImpl` on `ComponentController::Stop()`.
      instance->SetOnStop(
          [local_component = std::move(local_component)]() { local_component->OnStop(); });
    };
    auto on_exit = [this, local_component_factory = std::move(local_component_factory),
                    name]() mutable {
      // Drop the ComponentInstance. This also causes the ComponentController
      // and LocalComponentImpl to be dropped. Dropping the LocalComponentImpl
      // drops the handles.
      ZX_ASSERT_MSG(running_components_.erase(name) == 1, "running component not erased");
      // return the factory back to the list of components that can be restarted
      ready_components_[name] = std::move(local_component_factory);
    };
    running_components_[name] = std::make_unique<LocalComponentInstance>(
        std::move(controller), dispatcher_, std::move(on_start), std::move(on_exit));
  }

  // Start the component instance.
  running_components_[name]->Start(std::move(handles));
}

bool LocalComponentRunner::ContainsReadyComponent(std::string name) const {
  return ready_components_.find(name) != ready_components_.cend();
}

std::unique_ptr<LocalComponentRunner> LocalComponentRunner::Builder::Build(
    async_dispatcher_t* dispatcher) {
  return std::make_unique<LocalComponentRunner>(std::move(components_), dispatcher);
}

void LocalComponentRunner::Builder::Register(std::string name, LocalComponentKind mock) {
  ZX_ASSERT_MSG(!Contains(name), "Local component with same name being added: %s", name.c_str());
  components_[name] = std::move(mock);
}

bool LocalComponentRunner::Builder::Contains(std::string name) const {
  return components_.find(name) != components_.cend();
}

}  // namespace internal
}  // namespace component_testing
