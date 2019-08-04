// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/test_loop.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/outgoing_directory.h>

#include <examples/fidl/gen/my_service.h>
#include <gtest/gtest.h>

namespace fidl {
namespace {

class EchoImpl : public fidl::examples::echo::Echo {
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override { callback(value); }
};

TEST(ServiceHandlerTest, ConnectAndInvoke) {
  async::TestLoop loop;

  // Setup server.
  fidl::ServiceHandler default_handler;
  fuchsia::examples::MyService::Handler my_service(&default_handler);
  EchoImpl foo_impl;
  fidl::BindingSet<fidl::examples::echo::Echo> foo_bindings;
  zx_status_t status = my_service.add_foo(foo_bindings.GetHandler(&foo_impl, loop.dispatcher()));
  ASSERT_EQ(ZX_OK, status);

  sys::OutgoingDirectory outgoing;
  status = outgoing.AddService<fuchsia::examples::MyService>(std::move(default_handler));
  ASSERT_EQ(ZX_OK, status);

  fidl::InterfaceHandle<fuchsia::io::Directory> root;
  status = outgoing.Serve(root.NewRequest().TakeChannel(), loop.dispatcher());
  ASSERT_EQ(ZX_OK, status);

  // Setup client.
  fidl::InterfaceHandle<fuchsia::io::Directory> svc;
  status = fdio_service_connect_at(root.channel().get(), "svc",
                                   svc.NewRequest().TakeChannel().release());
  ASSERT_EQ(ZX_OK, status);

  auto service = fidl::OpenServiceAt<fuchsia::examples::MyService>(svc);
  auto handle = service.foo().Connect();
  ASSERT_TRUE(handle.is_valid());

  fidl::examples::echo::EchoPtr foo;
  status = foo.Bind(std::move(handle), loop.dispatcher());
  ASSERT_EQ(ZX_OK, status);

  // Call member.
  bool replied = false;
  foo->EchoString("", [&replied](fidl::StringPtr value) { replied = true; });
  ASSERT_TRUE(loop.RunUntilIdle());
  ASSERT_TRUE(replied);
}

}  // namespace
}  // namespace fidl
