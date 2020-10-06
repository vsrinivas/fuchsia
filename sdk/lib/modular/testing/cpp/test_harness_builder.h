// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TESTING_CPP_TEST_HARNESS_BUILDER_H_
#define LIB_MODULAR_TESTING_CPP_TEST_HARNESS_BUILDER_H_

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fit/optional.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>

namespace modular_testing {

// TestHarnessBuilder is a utility for building a
// |fuchsia.modular.testing.TestHarnessSpec|. This utility provides methods for
// hosting environment services and routing intercepted components.
//
//
// SAMPLE USAGE:
//
// #include <lib/modular/testing/cpp/fake_component.h>
// #include <lib/modular/testing/cpp/test_harness_builder.h>
// #include <lib/modular/testing/cpp/test_harness_launcher.h>
//
// class MyTest : gtest::RealLoopFixture {};
//
// TEST_F(MyTest, TestOne) {
//   modular_testing::TestHarnessLauncher test_harness_launcher;
//   modular_testing::TestHarnessBuilder builder;
//
//   // Instruct the test harness to intercept the launch of a new component
//   // within the test harness environment. Specify that the component should
//   // include foo.Service within its component manifest.
//   modular_testing::FakeComponent component(
//       {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
//        .sandbox_services = {"foo.Service"}});
//   builder.InterceptComponent(component.BuildInterceptOptions());
//
//   // Start an instance of the modular runtime in the test harness
//   // environment. As soon as |component_url| is created in
//   // this environment |component.on_create| is triggered.
//   builder.BuildAndRun(test_harness_launcher.test_harness());
//
//   // ... do something that would cause |component_url| to be created ...
//   RunLoopUntil([&] { return component.is_running(); });
//
//   foo::ServicePtr service_ptr;
//   component.component_context()->svc()->Connect(service_ptr.NewRequest());
//
//   // ...
// }
class TestHarnessBuilder final {
 public:
  using LaunchHandler =
      fit::function<void(fuchsia::sys::StartupInfo startup_info,
                         fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
                             intercepted_component)>;

  struct InterceptOptions {
    // The URL of the component to intercept. Use GenerateFakeUrl() to create a
    // random valid URL.
    //
    // Required: Must not be empty.
    std::string url;

    // A list of service names to populate the component's manifest
    // sandbox.services JSON property
    //
    // Optional.
    std::vector<std::string> sandbox_services;

    // Called when this component is launched.
    //
    // Required.
    LaunchHandler launch_handler;
  };

  // Builds on top of an empty |fuchsia.modular.testing.TestHarnessSpec|.
  TestHarnessBuilder();
  // Builds on top of the supplied |spec|.
  explicit TestHarnessBuilder(fuchsia::modular::testing::TestHarnessSpec spec);

  // Movable.
  TestHarnessBuilder(TestHarnessBuilder&&) = default;
  TestHarnessBuilder& operator=(TestHarnessBuilder&&) = default;
  // Not copyable.
  TestHarnessBuilder(const TestHarnessBuilder&) = delete;
  TestHarnessBuilder& operator=(const TestHarnessBuilder&) = delete;

  // Builds the underlying TestHarnessSpec and issues a |TestHarness/Run()|.
  // Binds an OnNewComponent event handler to the supplied |test_harness| to
  // route the Intercept*() calls issued below.
  //
  // Can only be called once.
  void BuildAndRun(const fuchsia::modular::testing::TestHarnessPtr& test_harness);

  // Amends the TestHarnessSpec to include interception instructions specified by
  // |options|.
  TestHarnessBuilder& InterceptComponent(InterceptOptions options);

  // Convenience variant of InterceptComponent() which adds a session shell URL
  // to the ModularConfig for |options.url|.
  TestHarnessBuilder& InterceptSessionShell(InterceptOptions options);

  // Convenience variant of InterceptComponent() which sets the story shell URL
  // in the ModularConfig to |options.url|.
  TestHarnessBuilder& InterceptStoryShell(InterceptOptions options);

  // Convenience variant of InterceptComponent() which sets the `session_launcher` URL
  // in the ModularConfig to |options.url|, and optionally, the `session_launcher` args.
  TestHarnessBuilder& InterceptSessionLauncherComponent(
      InterceptOptions options, fit::optional<std::vector<std::string>> args = fit::nullopt);

  // Make a service named |service_name| available in the test harness
  // environment. |connector| is called every time a client requests to
  // establish a new connection. This service is hosted for as long as this
  // TestHarnessBuilder object is kept alive.
  TestHarnessBuilder& AddService(const std::string& service_name,
                                 vfs::Service::Connector connector);

  // Make the templated |Interface| service available in the test harness
  // environment. |request_handler| is called every time a client requests to
  // establish a new connection. This service is hosted for as long as this
  // TestHarnessBuilder object is kept alive.
  template <typename Interface>
  TestHarnessBuilder& AddService(fidl::InterfaceRequestHandler<Interface> request_handler) {
    return AddService(Interface::Name_,
                      [request_handler = std::move(request_handler)](
                          zx::channel request, async_dispatcher_t* dispatcher) mutable {
                        request_handler(fidl::InterfaceRequest<Interface>(std::move(request)));
                      });
  }

  // Make the specified |service_name| available in the test harness
  // environment. The service is provided by |component_url|, which is
  // launched and kept alive for the duration of the test harness environment.
  // See |TestHarnessSpec.env_services.services_from_components| for more
  // details.
  TestHarnessBuilder& AddServiceFromComponent(const std::string& service_name,
                                              const std::string& component_url);

  // Make the templated service available in the test harness environment.
  // The service is provided by the given |component_url|, which is launched and
  // kept alive for the duration of the test harness environment. See
  // |TestHarnessSpec.env_services.services_from_components| for more details.
  template <typename Interface>
  TestHarnessBuilder& AddServiceFromComponent(const std::string& component_url) {
    return AddServiceFromComponent(Interface::Name_, component_url);
  }

  // Make the specified |service_name| from |services| available in the test
  // harness environment. |services| and the service are both kept alive for the
  // duration of this builder object's life time.
  TestHarnessBuilder& AddServiceFromServiceDirectory(
      const std::string& service_name, std::shared_ptr<sys::ServiceDirectory> services);

  TestHarnessBuilder& UseSessionShellForStoryShellFactory();

  // Make the templated service from |services| available in the test
  // harness environment. |services| and the service are both kept alive for the
  // duration of this builder object's life time.
  template <typename Interface>
  TestHarnessBuilder& AddServiceFromServiceDirectory(
      std::shared_ptr<sys::ServiceDirectory> services) {
    return AddServiceFromServiceDirectory(Interface::Name_, std::move(services));
  }

  // Returns a generated fake URL. Subsequent calls to this method will generate
  // a different URL. If |name| is provided, adds its contents to the component
  // name. Non alpha-num characters (a-zA-Z0-9) are stripped.
  static std::string GenerateFakeUrl(std::string name = "");

 private:
  // Takes the TestHarnessSpec built so far with the builder functions below.
  //
  // Can only be called once.
  fuchsia::modular::testing::TestHarnessSpec BuildSpec();

  // Builds a router function which routes calls to the various handlers
  // provided to Intercept*() variants. Intended to be used as the handler for
  // TestHarness.events.OnNewComponent
  //
  // Can only be called once.
  LaunchHandler BuildOnNewComponentHandler();

  fuchsia::modular::testing::TestHarnessSpec spec_;

  // Map from url to handler to be called when that url's component
  // is created and intercepted.
  std::map<std::string, LaunchHandler> handlers_;

  // Hosts services injected using AddService() and InheritService().
  std::unique_ptr<vfs::PseudoDir> env_services_;
};

}  // namespace modular_testing

#endif  // LIB_MODULAR_TESTING_CPP_TEST_HARNESS_BUILDER_H_
