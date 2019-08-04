// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/service/cpp/service_directory.h>
#include <lib/sys/service/cpp/test_base.h>

#include <examples/fidl/gen/my_service.h>
#include <gmock/gmock-matchers.h>

namespace fidl {
namespace {

class ServiceDirectoryTest : public fidl::testing::TestBase {};

TEST_F(ServiceDirectoryTest, OpenServiceDirectoryAt) {
  InterfaceHandle<fuchsia::io::Directory> directory;
  zx_status_t status = fdio_ns_connect(ns(), "/svc", fuchsia::io::OPEN_RIGHT_READABLE,
                                       directory.NewRequest().TakeChannel().release());
  ASSERT_EQ(ZX_OK, status);

  auto service_directory = OpenServiceDirectoryAt<fuchsia::examples::MyService>(directory);
  ASSERT_TRUE(service_directory.is_valid());

  auto named_service_directory =
      OpenNamedServiceDirectoryAt(directory, "fuchsia.examples.MyService");
  ASSERT_TRUE(named_service_directory.is_valid());

  auto explicit_service_directory =
      OpenNamedServiceDirectoryAt(directory, "/svc/fuchsia.examples.MyService");
  ASSERT_FALSE(explicit_service_directory.is_valid());
}

TEST_F(ServiceDirectoryTest, OpenServiceDirectoryIn) {
  auto service_directory = OpenServiceDirectoryIn<fuchsia::examples::MyService>(ns());
  ASSERT_TRUE(service_directory.is_valid());

  auto named_service_directory = OpenNamedServiceDirectoryIn(ns(), "fuchsia.examples.MyService");
  ASSERT_TRUE(named_service_directory.is_valid());

  auto explicit_service_directory =
      OpenNamedServiceDirectoryIn(ns(), "/svc/fuchsia.examples.MyService");
  ASSERT_TRUE(explicit_service_directory.is_valid());
}

TEST_F(ServiceDirectoryTest, ListInstances) {
  auto service_directory = OpenServiceDirectoryIn<fuchsia::examples::MyService>(ns());
  ASSERT_TRUE(service_directory.is_valid());

  auto instances = service_directory.ListInstances();
  EXPECT_THAT(instances, ::testing::UnorderedElementsAre("default", "my_instance"));
}

}  // namespace
}  // namespace fidl
