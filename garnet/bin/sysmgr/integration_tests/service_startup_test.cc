// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples/echo/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/logging.h>
#include <test/sysmgr/cpp/fidl.h>

#include "garnet/bin/appmgr/appmgr.h"
#include "gtest/gtest.h"

namespace sysmgr {
namespace test {
namespace {

using TestSysmgr = ::gtest::RealLoopFixture;

TEST_F(TestSysmgr, ServiceStartup) {
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  std::vector<std::string> sysmgr_args;
  // When auto_update_packages=true, this tests that the presence of amber
  // in the sys environment allows component loading to succeed. It should work
  // with a mocked amber.
  constexpr char kSysmgrConfig[] = R"(--config=
{
  "services": {
    "test.sysmgr.Interface": "fuchsia-pkg://fuchsia.com/sysmgr_integration_tests#meta/test_sysmgr_service.cmx",
    "fuchsia.pkg.PackageResolver": "fuchsia-pkg://fuchsia.com/sysmgr_integration_tests#meta/mock_resolver.cmx"
  },
  "startup_services": [
    "fuchsia.pkg.PackageResolver"
  ],
  "update_dependencies": [
    "fuchsia.pkg.PackageResolver"
  ]
})";
  sysmgr_args.push_back(kSysmgrConfig);

  auto environment_services = sys::ComponentContext::Create()->svc();

  // Make fidl.examples.echo.Echo from our own environment available in appmgr's
  // root realm.
  fuchsia::sys::ServiceListPtr root_realm_services(new fuchsia::sys::ServiceList);
  root_realm_services->names = std::vector<std::string>{fidl::examples::echo::Echo::Name_};
  root_realm_services->host_directory = environment_services->CloneChannel().TakeChannel();

  component::AppmgrArgs args{.pa_directory_request = h2.release(),
                             .root_realm_services = std::move(root_realm_services),
                             .environment_services = std::move(environment_services),
                             .sysmgr_url = "fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx",
                             .sysmgr_args = std::move(sysmgr_args),
                             .run_virtual_console = false,
                             .retry_sysmgr_crash = false};
  component::Appmgr appmgr(dispatcher(), std::move(args));

  // h1 is connected to h2, which is injected above as appmgr's
  // PA_DIRECTORY_REQUEST handle. appmgr hosts a directory on that handle, which
  // includes a svc/ subdirectory, which in turn connects to the first realm's
  // services. That first realm is the sys realm created by sysmgr, so
  // sysmgr_svc ends up being a directory with all services in the sys realm.
  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));
  ASSERT_EQ(ZX_OK, fdio_service_connect_at(h1.get(), "svc", svc_server.release()));
  sys::ServiceDirectory sysmgr_svc(std::move(svc_client));

  bool received_response = false;
  std::string response;
  ::test::sysmgr::InterfacePtr interface_ptr;
  ASSERT_EQ(ZX_OK, sysmgr_svc.Connect(interface_ptr.NewRequest(dispatcher())));

  interface_ptr->Ping([&](fidl::StringPtr r) {
    received_response = true;
    response = r.value_or("");
  });
  RunLoopUntil([&] { return received_response; });
  EXPECT_EQ("test_sysmgr_service_startup", response);

  const std::string echo_msg = "test string for echo";
  received_response = false;
  fidl::examples::echo::EchoPtr echo_ptr;
  ASSERT_EQ(ZX_OK, sysmgr_svc.Connect(echo_ptr.NewRequest(dispatcher())));

  echo_ptr->EchoString(echo_msg, [&](fidl::StringPtr r) {
    received_response = true;
    response = r.value_or("");
  });
  RunLoopUntil([&] { return received_response; });
  EXPECT_EQ(echo_msg, response);
}

}  // namespace
}  // namespace test
}  // namespace sysmgr
