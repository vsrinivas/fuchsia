// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/util.h>
#include <lib/gtest/test_with_message_loop.h>
#include <sysmgr/test/cpp/fidl.h>

#include "garnet/bin/appmgr/appmgr.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fxl/logging.h"

namespace sysmgr {
namespace test {
namespace {

class TestSysmgr : public gtest::TestWithMessageLoop {};

TEST_F(TestSysmgr, ServiceStartup) {
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::VectorPtr<fidl::StringPtr> sysmgr_args;
  sysmgr_args.push_back(
      "--config={\"services\": { \"sysmgr.test.Interface\": "
      "\"test_sysmgr_service_startup\" } }");
  sysmgr_args.push_back("--test");
  fuchsia::sys::AppmgrArgs args{.pa_directory_request = h2.release(),
                                .sysmgr_url = "sysmgr",
                                .sysmgr_args = std::move(sysmgr_args),
                                .run_virtual_console = false,
                                .retry_sysmgr_crash = false};
  fuchsia::sys::Appmgr appmgr(message_loop_.async(), std::move(args));

  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));
  ASSERT_EQ(ZX_OK,
            fdio_service_connect_at(h1.get(), "svc", svc_server.release()));

  sysmgr::test::InterfacePtr interface_ptr;
  ASSERT_EQ(ZX_OK, fdio_service_connect_at(
                       svc_client.get(), sysmgr::test::Interface::Name_,
                       interface_ptr.NewRequest(message_loop_.async())
                           .TakeChannel()
                           .release()));

  bool received_response = false;
  std::string response;
  interface_ptr->Ping([&received_response, &response](fidl::StringPtr r) {
    received_response = true;
    response = r;
  });

  RunLoopUntilWithTimeout([&received_response] { return received_response; },
                          fxl::TimeDelta::FromSeconds(10));
  EXPECT_EQ("test_sysmgr_service_startup", response);
}

}  // namespace
}  // namespace test
}  // namespace sysmgr
