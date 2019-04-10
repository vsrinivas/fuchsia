// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_IMPL_H_
#define LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/app_driver/cpp/agent_driver.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_interceptor.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>

namespace modular::testing {

// Provides the |TestHarness| service.
class TestHarnessImpl final : fuchsia::modular::testing::TestHarness {
 public:
  // |parent_env| is the environment under which a new hermetic test harness
  // environment is launched. |parent_env| must outlive this instance, otherwise
  // the test harness environment dies.
  //
  // |test_harness_request| is implemented by this class. The TestHarness
  // FIDL interface is the way to interact with the TestHarness API.
  TestHarnessImpl(const fuchsia::sys::EnvironmentPtr& parent_env,
                  fidl::InterfaceRequest<TestHarness> test_harness_request);

  virtual ~TestHarnessImpl() override;

  // Not copyable.
  TestHarnessImpl(const TestHarnessImpl&) = delete;
  void operator=(const TestHarnessImpl&) = delete;

 private:
  class InterceptedComponentImpl;
  class InterceptedSessionAgent;

  // Services requested using |TestHarness.GetService()| are provided by a
  // session agent which is started as part of the test harness' modular runtime
  // instance. This session agent is intercepted and implemented by the
  // |InterceptedSessionAgent| inner class. This struct holds state or the
  // intercepted session agent implementation.
  struct InterceptedSessionAgentInfo {
    // Service requests from |TestHarness.GetService()| may be issued before the
    // session agent, which provides these services, has been initialized. These
    // service requests are buffered here until the session agent has been
    // initialized.
    //
    // Flushed using FlushBufferedSessionAgentServices().
    struct BufferedServiceRequest {
      std::string service_name;
      zx::channel service_request;
    };
    std::vector<BufferedServiceRequest> buffered_service_requests;

    // The session agent's intercepted state that we must keep around to keep
    // the component alive:
    std::unique_ptr<component::StartupContext> component_context;
    std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component;
    std::unique_ptr<::modular::AgentDriver<InterceptedSessionAgent>>
        agent_driver;
  };

  // |fuchsia::modular::testing::TestHarness|
  void Run(fuchsia::modular::testing::TestHarnessSpec spec) override;

  // |fuchsia::modular::testing::TestHarness|
  void GetService(
      fuchsia::modular::testing::TestHarnessService service) override;

  [[nodiscard]] static std::vector<std::string> MakeBasemgrArgs(
      const fuchsia::modular::testing::TestHarnessSpec& spec);

  // Helper class
  fuchsia::modular::testing::InterceptedComponentPtr
  AddInterceptedComponentBinding(
      std::unique_ptr<sys::testing::InterceptedComponent>
          intercepted_component);

  // Use this helper method for checking fatal errors.
  //
  // If |status| is ZX_OK, returns |false|.
  // If |status| is not ZX_OK, the TestHarness binding is closed with |status|
  // as the epitaph, and returns |true|.
  bool CloseBindingIfError(zx_status_t status);

  // Sets up base shell interception (and dispatching a OnNewBaseShellEvent) if
  // the supplied |TestHarnessSpec| specifies it.
  [[nodiscard]] zx_status_t SetupBaseShellInterception();
  // Sets up session shell interception (and dispatching a OnNewBaseShellEvent)
  // if the supplied |TestHarnessSpec| specifies it.
  [[nodiscard]] zx_status_t SetupSessionShellInterception();
  // Sets up story shell interception (and dispatching a OnNewBaseShellEvent) if
  // the supplied |TestHarnessSpec| specifies it.
  [[nodiscard]] zx_status_t SetupStoryShellInterception();

  // Helper function for Setup*ShellInterception() methods above; actually
  // sets up the interception.
  [[nodiscard]] zx_status_t SetupShellInterception(
      const fuchsia::modular::testing::ShellSpec& shell_spec,
      sys::testing::ComponentInterceptor::ComponentLaunchHandler
          fake_interception_callback);

  // Sets up component intercept specified in
  // |TestHarnessSpec.components_to_intercept|.
  [[nodiscard]] zx_status_t SetupComponentInterception();

  // Sets up interception for the session agent which is launched as part of the
  // modular runtime. This session agent provides the services for
  // |TestHarness.GetServices()|.
  [[nodiscard]] zx_status_t SetupFakeSessionAgent();

  // Buffers service request from |GetService()|.
  // FlushBufferedSessionAgentServices() processes these services once the
  // session agent supplying these services comes alive.
  template <typename Interface>
  void BufferSessionAgentService(fidl::InterfaceRequest<Interface> request) {
    intercepted_session_agent_info_.buffered_service_requests.push_back(
        InterceptedSessionAgentInfo::BufferedServiceRequest{
            Interface::Name_, request.TakeChannel()});
  }

  // Processes the service requests which are buffered from |GetService()|.
  void FlushBufferedSessionAgentServices();

  // The test harness environment is a child of |parent_env_|.
  const fuchsia::sys::EnvironmentPtr& parent_env_;  // Not owned.

  fidl::Binding<fuchsia::modular::testing::TestHarness> binding_;
  fuchsia::modular::testing::TestHarnessSpec spec_;

  // This map manages InterceptedComponent bindings (and their implementations).
  // When a |InterceptedComponent| connection is closed, it is automatically
  // removed from this map (and its impl is deleted as well).
  //
  // The key is the raw-pointer backing the unique_ptr value.
  std::map<InterceptedComponentImpl*, std::unique_ptr<InterceptedComponentImpl>>
      intercepted_component_impls_;

  // |interceptor_| must outlive |enclosing_env_|.
  sys::testing::ComponentInterceptor interceptor_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> enclosing_env_;
  fuchsia::sys::ComponentControllerPtr basemgr_ctrl_;

  InterceptedSessionAgentInfo intercepted_session_agent_info_;

  friend class TestHarnessImplTest;
};

}  // namespace modular::testing

#endif  // LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_IMPL_H_
