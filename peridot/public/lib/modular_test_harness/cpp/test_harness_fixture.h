// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_
#define LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <src/lib/fxl/logging.h>

namespace modular {
namespace testing {

// A gtest fixture and utility classes for tests that require an instance of
// the modular runtime.  Depends on the `modular_test_harness` fuchsia package.
//
// SAMPLE USAGE:
//
// #include <lib/modular_test_harness/cpp/test_harness_fixture.h>
// #include <lib/modular_test_harness/cpp/fake_component.h>
//
// class MyTest : public modular::testing::TestHarnessFixture {};
//
// TEST_F(MyTest, TestOne) {
//   modular::testing::TestHarnessBuilder builder;
//
//   // Instruct the test harness to intercept the launch of a new component
//   // within the test harness environment. Specify that the component should
//   // include foo.Service within its component manifest.
//   auto component_url = GenerateFakeUrl();
//   modular::testing::FakeComponent component;
//   builder.InterceptComponent(
//      component.GetOnCreateHandler(),
//      {.url = component_url,
//       .sandbox_services = {"foo.Service"}});
//
//   // Start an instance of the modular runtime in the test harness
//   // environment. As soon as |component_url| is created in
//   // this environment |component.on_create| is triggered.
//   test_harness().events().OnNewComponent =
//   builder.BuildOnNewComponentHandler();
//   test_harness()->Run(builder.BuildSpec());
//
//   // ... do something that would cause |component_url| to be created ...
//   RunLoopUntil([&] { return component.is_running(); });
//
//   foo::ServicePtr service_ptr;
//   component.component_context()->svc()->Connect(service_ptr.NewRequest());
//
//   // ...
// }
class TestHarnessBuilder {
 public:
  struct InterceptOptions {
    // The URL of the component to intercept. Use GenerateFakeUrl() to create a
    // random valid URL.
    //
    // Optional: if not provided, a URL is generated using GenerateFakeUrl().
    std::string url;

    // A list of service names to populate the component's manifest
    // sandbox.services JSON property
    //
    // Optional.
    std::vector<std::string> sandbox_services;
  };

  using OnNewComponentHandler = fit::function<void(
      fuchsia::sys::StartupInfo startup_info,
      fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
          intercepted_component)>;

  // Takes the TestHarnessSpec built so far with the builder functions below.
  //
  // Can only be called once.
  fuchsia::modular::testing::TestHarnessSpec BuildSpec();

  // Builds a router function which routes calls to the various handlers
  // provided to Intercept*() variants. Intended to be used as the handler for
  // TestHarness.events.OnNewComponent
  //
  // Can only be called once.
  OnNewComponentHandler BuildOnNewComponentHandler();

  // Amends the TestHarnessSpec to include interception instructions based on
  // |options| and stores |on_create| for use in the router function created
  // through BuildOnNewComponentHandler().
  TestHarnessBuilder& InterceptComponent(
      OnNewComponentHandler on_new_component,
      InterceptOptions options = InterceptOptions());

  // Convenience variant of InterceptComponent() which sets the base shell URL
  // in the ModularConfig to |options.url|.
  TestHarnessBuilder& InterceptBaseShell(
      OnNewComponentHandler on_new_component,
      InterceptOptions options = InterceptOptions());

  // Convenience variant of InterceptComponent() which adds a session shell URL
  // to the ModularConfig for |options.url|.
  TestHarnessBuilder& InterceptSessionShell(
      OnNewComponentHandler on_new_component,
      InterceptOptions options = InterceptOptions());

  // Convenience variant of InterceptComponent() which sets the story shell URL
  // in the ModularConfig to |options.url|.
  TestHarnessBuilder& InterceptStoryShell(
      OnNewComponentHandler on_new_component,
      InterceptOptions options = InterceptOptions());

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
  TestHarnessBuilder& AddService(
      fidl::InterfaceRequestHandler<Interface> request_handler) {
    return AddService(
        Interface::Name_,
        [request_handler = std::move(request_handler)](
            zx::channel request, async_dispatcher_t* dispatcher) mutable {
          request_handler(
              fidl::InterfaceRequest<Interface>(std::move(request)));
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
  TestHarnessBuilder& AddServiceFromComponent(
      const std::string& component_url) {
    return AddServiceFromComponent(Interface::Name_, component_url);
  }

  // Make the specified |service_name| from |services| available in the test
  // harness environment. |services| and the service are both kept alive for the
  // duration of this builder object's life time.
  TestHarnessBuilder& AddServiceFromServiceDirectory(
      const std::string& service_name,
      std::shared_ptr<sys::ServiceDirectory> services);

  // Make the templated service from |services| available in the test
  // harness environment. |services| and the service are both kept alive for the
  // duration of this builder object's life time.
  template <typename Interface>
  TestHarnessBuilder& AddServiceFromServiceDirectory(
      std::shared_ptr<sys::ServiceDirectory> services) {
    return AddServiceFromServiceDirectory(Interface::Name_,
                                          std::move(services));
  }

  // Returns a generated fake URL. Subsequent calls to this method will generate
  // a different URL. If |name| is provided, adds its contents to the component
  // name. Non alpha-num characters (a-zA-Z0-9) are stripped.
  std::string GenerateFakeUrl(std::string name = "") const;

 private:
  fuchsia::modular::testing::TestHarnessSpec spec_;

  // Map from url to handler to be called when that url's component
  // is created and intercepted.
  std::map<std::string, OnNewComponentHandler> handlers_;

  // Hosts services injected using AddService() and InheritService().
  vfs::PseudoDir env_services_;
};

class TestHarnessFixture : public sys::testing::TestWithEnvironment {
 protected:
  TestHarnessFixture();
  virtual ~TestHarnessFixture();

  fuchsia::modular::testing::TestHarnessPtr& test_harness() {
    return test_harness_;
  }

  // DEPRECATED: Prefer to use the methods on TestHarnessBuilder.
  std::string InterceptBaseShell(
      fuchsia::modular::testing::TestHarnessSpec* spec) const;
  std::string InterceptSessionShell(
      fuchsia::modular::testing::TestHarnessSpec* spec,
      std::string extra_cmx_contents = "") const;
  std::string InterceptStoryShell(
      fuchsia::modular::testing::TestHarnessSpec* spec) const;

  // Starts a new mod by the given |intent| and |mod_name| in a new story given
  // by |story_name|.
  void AddModToStory(fuchsia::modular::Intent intent, std::string mod_name,
                     std::string story_name);

 private:
  std::shared_ptr<sys::ServiceDirectory> test_harness_svc_;
  fuchsia::sys::ComponentControllerPtr test_harness_ctrl_;
  fuchsia::modular::testing::TestHarnessPtr test_harness_;
};

}  // namespace testing
}  // namespace modular

#endif  // LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_
