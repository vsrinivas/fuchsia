// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <lib/sys/service/cpp/service.h>
#include <lib/sys/service/cpp/test_base.h>

namespace sys {
namespace {

class ServiceTest : public testing::TestBase {};

TEST_F(ServiceTest, OpenServiceAt) {
  fidl::InterfaceHandle<fuchsia::io::Directory> directory;
  zx_status_t status = fdio_ns_connect(ns(), "/svc", fuchsia::io::OPEN_RIGHT_READABLE,
                                       directory.NewRequest().TakeChannel().release());
  ASSERT_EQ(ZX_OK, status);

  auto default_service = OpenServiceAt<fuchsia::examples::EchoService>(directory);
  ASSERT_TRUE(default_service);

  auto service = OpenServiceAt<fuchsia::examples::EchoService>(directory, "my_instance");
  ASSERT_TRUE(service);

  auto named_service = OpenNamedServiceAt(directory, "fuchsia.examples.EchoService", "my_instance");
  ASSERT_TRUE(named_service);

  auto explicit_service =
      OpenNamedServiceAt(directory, "/svc/fuchsia.examples.EchoService", "my_instance");
  ASSERT_FALSE(explicit_service);
}

TEST_F(ServiceTest, OpenServiceIn) {
  auto default_service = OpenServiceIn<fuchsia::examples::EchoService>(ns());
  ASSERT_TRUE(default_service);

  auto service = OpenServiceIn<fuchsia::examples::EchoService>(ns(), "my_instance");
  ASSERT_TRUE(service);

  auto named_service = OpenNamedServiceIn(ns(), "fuchsia.examples.EchoService", "my_instance");
  ASSERT_TRUE(named_service);

  auto explicit_service =
      OpenNamedServiceIn(ns(), "/svc/fuchsia.examples.EchoService", "my_instance");
  ASSERT_TRUE(explicit_service);
}

}  // namespace
}  // namespace sys
