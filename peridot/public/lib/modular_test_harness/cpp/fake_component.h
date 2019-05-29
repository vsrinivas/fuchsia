// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TEST_HARNESS_CPP_FAKE_COMPONENT_H_
#define LIB_MODULAR_TEST_HARNESS_CPP_FAKE_COMPONENT_H_

#include <lib/sys/cpp/component_context.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>

namespace modular {
namespace testing {

// Represents an instance of an intercepted component. Clients may use directly
// or sub-class and override OnCreate() and/or OnDestroy().
//
// Consumes a fuchsia.sys.StartupInfo and
// fuchsia.modular.testing.InterceptedComponent handle, constructs a
// sys.ComponentContext and forwards lifecycle signals to virtual functions.
//
// Usage: pass ComponentBase.GetOnCreateHandler() to TestHarnessBuilder's
// Intercept*() methods.
class FakeComponent {
 public:
  virtual ~FakeComponent();

  // Returns a binder function that initializes members, dispatches OnCreate()
  // and wires OnDestroy() to the InterceptedComponent.OnKill event.
  TestHarnessBuilder::OnNewComponentHandler GetOnCreateHandler();

  // Returns true if the component was launched by the component manager and
  // has not yet been destroyed.
  bool is_running() const;

  // Returns the ComponentContext for the running component.
  //
  // Requires: is_running()
  sys::ComponentContext* component_context();

  // Instructs the component manager that this component is exiting. See
  // documentation for fuchsia.sys.TerminationReason for more details.
  //
  // Requires: is_running()
  void Exit(int64_t exit_code, fuchsia::sys::TerminationReason reason =
                                   fuchsia::sys::TerminationReason::EXITED);

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

 private:
  fuchsia::modular::testing::InterceptedComponentPtr intercepted_component_ptr_;
  std::unique_ptr<sys::ComponentContext> component_context_;
};

}  // namespace testing
}  // namespace modular

#endif  // LIB_MODULAR_TEST_HARNESS_CPP_FAKE_COMPONENT_H_
