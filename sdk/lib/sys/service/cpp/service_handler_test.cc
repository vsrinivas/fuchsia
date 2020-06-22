// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <fuchsia/examples/cpp/fidl_test_base.h>
#include <lib/async-testing/test_loop.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/outgoing_directory.h>

#include <gtest/gtest.h>

namespace sys {
namespace {

class EchoImpl : public fuchsia::examples::testing::Echo_TestBase {
  void EchoString(std::string value, EchoStringCallback callback) override { callback(value); }
  void NotImplemented_(const std::string& name) override {
    ASSERT_TRUE(false) << "Method not implemented: " << name << std::endl;
  }
};

TEST(ServiceHandlerTest, ConnectAndInvoke) {
  async::TestLoop loop;

  // Setup server.
  sys::ServiceHandler default_handler;
  fuchsia::examples::EchoService::Handler my_service(&default_handler);
  EchoImpl regular_impl;
  fidl::BindingSet<fuchsia::examples::Echo> regular_echo_bindings;
  zx_status_t status = my_service.add_regular_echo(
      regular_echo_bindings.GetHandler(&regular_impl, loop.dispatcher()));
  ASSERT_EQ(ZX_OK, status);

  sys::OutgoingDirectory outgoing;
  status = outgoing.AddService<fuchsia::examples::EchoService>(std::move(default_handler));
  ASSERT_EQ(ZX_OK, status);

  fidl::InterfaceHandle<fuchsia::io::Directory> root;
  status = outgoing.Serve(root.NewRequest().TakeChannel(), loop.dispatcher());
  ASSERT_EQ(ZX_OK, status);

  // Setup client.
  fidl::InterfaceHandle<fuchsia::io::Directory> svc;
  status = fdio_service_connect_at(root.channel().get(), "svc",
                                   svc.NewRequest().TakeChannel().release());
  ASSERT_EQ(ZX_OK, status);

  auto service = OpenServiceAt<fuchsia::examples::EchoService>(svc);
  auto handle = service.regular_echo().Connect();
  ASSERT_TRUE(handle.is_valid());

  fuchsia::examples::EchoPtr regular_echo;
  status = regular_echo.Bind(std::move(handle), loop.dispatcher());
  ASSERT_EQ(ZX_OK, status);

  // Call member.
  bool replied = false;
  regular_echo->EchoString("", [&replied](std::string value) { replied = true; });
  ASSERT_TRUE(loop.RunUntilIdle());
  ASSERT_TRUE(replied);
}

}  // namespace
}  // namespace sys
