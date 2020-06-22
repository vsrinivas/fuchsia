// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <lib/sys/service/cpp/service_aggregate.h>
#include <lib/sys/service/cpp/test_base.h>

#include <gmock/gmock.h>

namespace sys {
namespace {

class ServiceAggregateTest : public testing::TestBase {};

TEST_F(ServiceAggregateTest, OpenServiceAggregateAt) {
  fidl::InterfaceHandle<fuchsia::io::Directory> directory;
  zx_status_t status = fdio_ns_connect(ns(), "/svc", fuchsia::io::OPEN_RIGHT_READABLE,
                                       directory.NewRequest().TakeChannel().release());
  ASSERT_EQ(ZX_OK, status);

  auto service_aggregate = OpenServiceAggregateAt<fuchsia::examples::EchoService>(directory);
  ASSERT_TRUE(service_aggregate.is_valid());

  auto named_service_aggregate =
      OpenNamedServiceAggregateAt(directory, "fuchsia.examples.EchoService");
  ASSERT_TRUE(named_service_aggregate.is_valid());

  auto explicit_service_aggregate =
      OpenNamedServiceAggregateAt(directory, "/svc/fuchsia.examples.EchoService");
  ASSERT_FALSE(explicit_service_aggregate.is_valid());
}

TEST_F(ServiceAggregateTest, OpenServiceAggregateIn) {
  auto service_aggregate = OpenServiceAggregateIn<fuchsia::examples::EchoService>(ns());
  ASSERT_TRUE(service_aggregate.is_valid());

  auto named_service_aggregate = OpenNamedServiceAggregateIn(ns(), "fuchsia.examples.EchoService");
  ASSERT_TRUE(named_service_aggregate.is_valid());

  auto explicit_service_aggregate =
      OpenNamedServiceAggregateIn(ns(), "/svc/fuchsia.examples.EchoService");
  ASSERT_TRUE(explicit_service_aggregate.is_valid());
}

TEST_F(ServiceAggregateTest, ListInstances) {
  auto service_aggregate = OpenServiceAggregateIn<fuchsia::examples::EchoService>(ns());
  ASSERT_TRUE(service_aggregate.is_valid());

  auto instances = service_aggregate.ListInstances();
  EXPECT_THAT(instances, ::testing::UnorderedElementsAre("default", "my_instance"));
}

}  // namespace
}  // namespace sys
