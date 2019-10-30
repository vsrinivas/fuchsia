// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TESTING_CPP_FAKE_COMPONENT_H_
#define LIB_MODULAR_TESTING_CPP_FAKE_COMPONENT_H_

#include <lib/fidl/cpp/binding_set.h>
#include <lib/modular/testing/cpp/test_harness_builder.h>
#include <lib/sys/cpp/component_context.h>

namespace modular_testing {

// FakeComponent is a utility class for intercepting modular components. This class is
// designed to be used alongside the modular_testing::TestHarnessBuilder::Intercept*() methods.
// Clients may instantiate this class directly, or sub-class and override OnCreate() and
// OnDestroy().
//
// See |modular_testing::FakeAgent| for intercepting a modular Agent.
//
// USAGE:
// ======
// * Pass BuildInterceptOptions() to TestHarnessBuilder::InterceptComponent() to route the
//   component's launch to this instance.
// * Use is_running() to determine if the component is running.
// * Use component_context() to connect to incoming services, and add public services.
//
// EXAMPLE:
// ========
// ..
// modular_testing::TestHarnessBuilder builder;
// modular_testing::FakeComponent fake_component(
//     {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
// builder.InterceptComponent(fake_component.BuildInterceptOptions());
// builder.BuildAndRun(test_harness());
// ..
class FakeComponent : fuchsia::modular::Lifecycle {
 public:
  struct Args {
    // Required.
    //
    // The URL of this component.
    std::string url;

    // Optional.
    std::vector<std::string> sandbox_services;
  };

  FakeComponent() = delete;
  explicit FakeComponent(Args args);

  FakeComponent(const FakeComponent&) = delete;
  FakeComponent(FakeComponent&&) = delete;
  void operator=(const FakeComponent&) = delete;
  void operator=(FakeComponent&&) = delete;

  ~FakeComponent() override;

  // Constructs an InterceptOptions struct using the FakeComponent::Args supplied to this instance.
  //
  // The constructed launch handler takes care of triggering OnCreate() on launch, OnDestroy() on
  // destruction.
  //
  // |dispatcher| is used for serving the component's outgoing directory and dispatching
  // |OnDestroy()|. A value of |nullptr| will use the current thread's dispatcher.
  modular_testing::TestHarnessBuilder::InterceptOptions BuildInterceptOptions(
      async_dispatcher_t* dispatcher = nullptr);

  // Returns the URL assigned to this component; see |Args::url|.
  std::string url() const;

  // Returns true if the component was launched by the component manager and
  // has not yet been destroyed.
  bool is_running() const;

  // Returns the ComponentContext for the running component.
  //
  // Requires: is_running()
  sys::ComponentContext* component_context();
  const sys::ComponentContext* component_context() const;

  // Instructs the component manager that this component is exiting. See
  // documentation for fuchsia.sys.TerminationReason for more details.
  //
  // Requires: is_running()
  void Exit(int64_t exit_code,
            fuchsia::sys::TerminationReason reason = fuchsia::sys::TerminationReason::EXITED);

 protected:
  // Called when the component is created.  The directory handles for "/svc" in
  // |startup_info.flat_namespace| and that for
  // |startup_info.launch_info.directory_request| will be invalid: they are
  // both consumed in the construction of |component_context_|.
  //
  // Clients may override this to be notified of create as well as to consume
  // remaining |startup_info.flat_namespace| entries.
  virtual void OnCreate(fuchsia::sys::StartupInfo startup_info) {}

  // Called when |intercepted_componet_ptr_|'s OnKill event is dispatched.
  //
  // Clients may override this to be notifed of component destruction.
  virtual void OnDestroy() {}

  // Called when this component receives a fuchsia.modular.Lifecycle/Terminate(). This object will
  // |Exit()| when Terminate() is received.
  //
  // |fuchsia::modular::Lifecycle|
  void Terminate() override;

 private:
  Args args_;

  fuchsia::modular::testing::InterceptedComponentPtr intercepted_component_ptr_;
  std::unique_ptr<sys::ComponentContext> component_context_;

  fidl::BindingSet<fuchsia::modular::Lifecycle> lifecycle_bindings_;
};

}  // namespace modular_testing

#endif  // LIB_MODULAR_TESTING_CPP_FAKE_COMPONENT_H_
