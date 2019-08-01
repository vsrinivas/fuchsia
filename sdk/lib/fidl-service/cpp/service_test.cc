// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl-service/cpp/service.h>
#include <lib/fidl-service/cpp/test_base.h>

#include <examples/fidl/gen/my_service.h>

namespace fidl {
namespace {

class ServiceTest : public fidl::testing::TestBase {};

TEST_F(ServiceTest, OpenServiceAt) {
  InterfaceHandle<fuchsia::io::Directory> directory;
  zx_status_t status = fdio_ns_connect(ns(), "/svc", fuchsia::io::OPEN_RIGHT_READABLE,
                                       directory.NewRequest().TakeChannel().release());
  ASSERT_EQ(ZX_OK, status);

  auto default_service = OpenServiceAt<fuchsia::examples::MyService>(directory);
  ASSERT_TRUE(default_service);

  auto service = OpenServiceAt<fuchsia::examples::MyService>(directory, "my_instance");
  ASSERT_TRUE(service);

  auto named_service = OpenNamedServiceAt(directory, "fuchsia.examples.MyService", "my_instance");
  ASSERT_TRUE(named_service.is_valid());

  auto explicit_service =
      OpenNamedServiceAt(directory, "/svc/fuchsia.examples.MyService", "my_instance");
  ASSERT_FALSE(explicit_service.is_valid());
}

TEST_F(ServiceTest, OpenServiceIn) {
  auto default_service = OpenServiceIn<fuchsia::examples::MyService>(ns());
  ASSERT_TRUE(default_service);

  auto service = OpenServiceIn<fuchsia::examples::MyService>(ns(), "my_instance");
  ASSERT_TRUE(service);

  auto named_service = OpenNamedServiceIn(ns(), "fuchsia.examples.MyService", "my_instance");
  ASSERT_TRUE(named_service.is_valid());

  auto explicit_service =
      OpenNamedServiceIn(ns(), "/svc/fuchsia.examples.MyService", "my_instance");
  ASSERT_TRUE(explicit_service.is_valid());
}

}  // namespace
}  // namespace fidl
