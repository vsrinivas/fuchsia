// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/util.h>
#include <lib/gtest/real_loop_fixture.h>
#include <test/sysmgr/cpp/fidl.h>
#include "garnet/bin/appmgr/appmgr.h"
#include "gtest/gtest.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fxl/logging.h"

namespace sysmgr {
namespace test {
namespace {

using TestSysmgr = ::gtest::RealLoopFixture;

TEST_F(TestSysmgr, ServiceStartup) {
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::VectorPtr<fidl::StringPtr> sysmgr_args;
  // When auto_update_packages=true, this tests that the presence of amber
  // in the sys environment allows component loading to succeed. It should work
  // with a mocked amber.
  constexpr char kSysmgrConfig[] = R"(--config=
{
  "services": {
    "test.sysmgr.Interface": "fuchsia-pkg://fuchsia.com/sysmgr_integration_tests#meta/test_sysmgr_service.cmx",
    "fuchsia.amber.Control": "fuchsia-pkg://fuchsia.com/sysmgr_integration_tests#meta/mock_amber.cmx"
  },
  "startup_services": [
    "fuchsia.amber.Control"
  ],
  "update_dependencies": [
    "fuchsia.amber.Control"
  ]
})";
  sysmgr_args.push_back(kSysmgrConfig);

  auto context = component::StartupContext::CreateFromStartupInfo();

  component::AppmgrArgs args{
      .pa_directory_request = h2.release(),
      .environment_services = context->incoming_services(),
      .sysmgr_url = "fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx",
      .sysmgr_args = std::move(sysmgr_args),
      .run_virtual_console = false,
      .retry_sysmgr_crash = false};
  component::Appmgr appmgr(dispatcher(), std::move(args));

  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));
  ASSERT_EQ(ZX_OK,
            fdio_service_connect_at(h1.get(), "svc", svc_server.release()));

  ::test::sysmgr::InterfacePtr interface_ptr;
  ASSERT_EQ(
      ZX_OK,
      fdio_service_connect_at(
          svc_client.get(), ::test::sysmgr::Interface::Name_,
          interface_ptr.NewRequest(dispatcher()).TakeChannel().release()));

  bool received_response = false;
  std::string response;
  interface_ptr->Ping([&received_response, &response](fidl::StringPtr r) {
    received_response = true;
    response = r;
  });

  RunLoopWithTimeoutOrUntil([&received_response] { return received_response; },
                            zx::sec(10));
  EXPECT_EQ("test_sysmgr_service_startup", response);
}

}  // namespace
}  // namespace test
}  // namespace sysmgr
