// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_IMPL_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/modular/cpp/agent.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_interceptor.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/vfs/cpp/pseudo_dir.h>

#include <set>

#include "src/modular/lib/lifecycle/cpp/lifecycle_impl.h"

namespace modular_testing {

constexpr char kSessionAgentFakeInterceptionUrl[] =
    "fuchsia-pkg://example.com/FAKE_SESSION_AGENT_PKG/fake_session_agent.cmx";

// Provides the |TestHarness| service.
class TestHarnessImpl final : fuchsia::modular::testing::TestHarness,
                              public modular::LifecycleImpl::Delegate {
 public:
  // |parent_env| is the environment under which a new hermetic test harness
  // environment is launched. |parent_env| must outlive this instance, otherwise
  // the test harness environment dies.
  //
  // |on_exit| is called if a running session instance terminates, or if the TestHarness interface
  // is closed, or terminates. This can happen if the TestHarness client drops their side of the
  // connection, or this class closes it due to an error; In this case, the error is sent as an
  // epitaph. See the |TestHarness| protocol documentation for more details.
  TestHarnessImpl(const fuchsia::sys::EnvironmentPtr& parent_env, fit::function<void()> on_exit);

  virtual ~TestHarnessImpl() override;

  // Not copyable.
  TestHarnessImpl(const TestHarnessImpl&) = delete;
  void operator=(const TestHarnessImpl&) = delete;

  // |request| is served by this class. The TestHarness FIDL interface is the way to interact with
  // the TestHarness API.
  void Bind(fidl::InterfaceRequest<fuchsia::modular::testing::TestHarness> request);

  // Terminate the running instance of the test harness. If there is a running session, it is asked
  // to terminate.
  //
  // |modular::LifecycleImpl::Delegate|
  void Terminate() override;

 private:
  class InterceptedComponentImpl;
  class InterceptedSessionAgent;

  // Services requested using |TestHarness.ConnectToService()| are provided by a
  // session agent which is started as part of the test harness' modular runtime
  // instance. This session agent is intercepted and implemented by the
  // |InterceptedSessionAgent| inner class. This struct holds state or the
  // intercepted session agent implementation.
  struct InterceptedSessionAgentInfo {
    // Service requests from |TestHarness.ConnectToService()| may be issued
    // before the session agent, which provides these services, has been
    // initialized. These service requests are buffered here until the session
    // agent has been initialized.
    //
    // Flushed using FlushBufferedSessionAgentServices().
    struct BufferedServiceRequest {
      std::string service_name;
      zx::channel service_request;
    };
    std::vector<BufferedServiceRequest> buffered_service_requests;

    // The session agent's intercepted state that we must keep around to keep
    // the component alive:
    std::unique_ptr<sys::ComponentContext> component_context;
    std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component;
    std::unique_ptr<::modular::Agent> agent;
  };

  // |fuchsia::modular::testing::TestHarness|
  void Run(fuchsia::modular::testing::TestHarnessSpec spec) override;

  // |fuchsia::modular::testing::TestHarness|
  void ConnectToModularService(fuchsia::modular::testing::ModularService service) override;

  // |fuchsia::modular::testing::TestHarness|
  void ConnectToEnvironmentService(std::string service_name, zx::channel request) override;

  // |fuchsia::modular::testing::TestHarness|
  void ParseConfig(std::string config, ParseConfigCallback callback) override;

  [[nodiscard]] static std::unique_ptr<vfs::PseudoDir> MakeBasemgrConfigDir(
      const fuchsia::modular::testing::TestHarnessSpec& spec);

  // Helper class
  fuchsia::modular::testing::InterceptedComponentPtr AddInterceptedComponentBinding(
      std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component);

  // Use this helper method for checking fatal errors.
  //
  // If |status| is ZX_OK, returns |false|.
  // If |status| is not ZX_OK, the TestHarness binding is closed with |status|
  // as the epitaph, and returns |true|.
  bool CloseBindingIfError(zx_status_t status);

  // Sets up component intercept specified in
  // |TestHarnessSpec.components_to_intercept|.
  [[nodiscard]] zx_status_t SetupComponentInterception();

  // Sets up interception for the session agent which is launched as part of the
  // modular runtime. This session agent provides the services for
  // |TestHarness.ConnectToModularService()|.
  [[nodiscard]] zx_status_t SetupFakeSessionAgent();

  // Sets up the enclosing environment with services specified in
  // |TestHarnessSpec.injected_services|, and removes them from the supplied
  // |default_injected_services|.
  //
  // |default_injected_services| maps service name => component URL which serves
  // it.
  void InjectServicesIntoEnvironment(sys::testing::EnvironmentServices* env_services,
                                     std::map<std::string, std::string>* default_injected_services);

  // Buffers service request from |GetService()|.
  // FlushBufferedSessionAgentServices() processes these services once the
  // session agent supplying these services comes alive.
  template <typename Interface>
  void BufferSessionAgentService(fidl::InterfaceRequest<Interface> request) {
    intercepted_session_agent_info_.buffered_service_requests.push_back(
        InterceptedSessionAgentInfo::BufferedServiceRequest{Interface::Name_,
                                                            request.TakeChannel()});

    FlushBufferedSessionAgentServices();
  }

  // Processes the service requests which are buffered from |GetService()|.
  void FlushBufferedSessionAgentServices();

  // Populates the test harness environment with services described by
  // |spec_.env_services|.
  zx_status_t PopulateEnvServices(sys::testing::EnvironmentServices* env_services);

  // Injects services into the test harness environment according to
  // |spec_.env_services.services_from_components| and
  // |spec_.env_services_to_inject|.
  //
  // Injected service names are inserted into |added_svcs|.
  zx_status_t PopulateEnvServicesWithComponents(sys::testing::EnvironmentServices* env_services,
                                                std::set<std::string>* added_svcs);

  // Injects services into the test harness environment according to
  // |spec_.env_services.service_dir|.
  //
  // Injected service names are inserted into |added_svcs|.
  zx_status_t PopulateEnvServicesWithServiceDir(sys::testing::EnvironmentServices* env_services,
                                                std::set<std::string>* added_svcs);

  // The test harness environment is a child of |parent_env_|.
  const fuchsia::sys::EnvironmentPtr& parent_env_;  // Not owned.

  fidl::Binding<fuchsia::modular::testing::TestHarness> binding_;
  fuchsia::modular::testing::TestHarnessSpec spec_;

  fit::function<void()> on_exit_;

  // This map manages InterceptedComponent bindings (and their
  // implementations). When a |InterceptedComponent| connection is closed, it
  // is automatically removed from this map (and its impl is deleted as well).
  //
  // The key is the raw-pointer backing the unique_ptr value.
  std::map<InterceptedComponentImpl*, std::unique_ptr<InterceptedComponentImpl>>
      intercepted_component_impls_;

  // |interceptor_| must outlive |enclosing_env_|.
  sys::testing::ComponentInterceptor interceptor_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> enclosing_env_;
  std::unique_ptr<vfs::PseudoDir> basemgr_config_dir_;
  fuchsia::sys::ComponentControllerPtr basemgr_ctrl_;
  fuchsia::modular::LifecyclePtr basemgr_lifecycle_;

  InterceptedSessionAgentInfo intercepted_session_agent_info_;

  std::unique_ptr<sys::ServiceDirectory> env_service_dir_;

  friend class TestHarnessImplTest;
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_IMPL_H_
