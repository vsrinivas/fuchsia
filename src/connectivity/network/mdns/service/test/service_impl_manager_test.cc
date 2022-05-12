// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/service_impl_manager.h"

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr.h>

#include "lib/gtest/real_loop_fixture.h"

namespace mdns {
namespace test {

class ServiceImplManagerTests : public gtest::RealLoopFixture {};

class TestServiceImpl : public fuchsia::net::mdns::ServiceInstanceResolver {
 public:
  TestServiceImpl(bool& set_on_deletion) : set_on_deletion_(set_on_deletion) {}

  ~TestServiceImpl() override { set_on_deletion_ = true; }

  void ResolveServiceInstance(std::string service, std::string instance, int64_t timeout,
                              ResolveServiceInstanceCallback callback) override {}

  bool& set_on_deletion_;
};

// Tests normal use.
TEST_F(ServiceImplManagerTests, NormalUse) {
  fit::closure deleter_from_create;
  bool instance_deleted = false;

  ServiceImplManager<fuchsia::net::mdns::ServiceInstanceResolver> under_test(
      [&deleter_from_create, &instance_deleted](
          fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstanceResolver> request,
          fit::closure deleter) {
        deleter_from_create = std::move(deleter);
        return std::make_unique<TestServiceImpl>(instance_deleted);
      });

  fidl::InterfacePtr<fuchsia::net::mdns::ServiceInstanceResolver> pointer;
  under_test.Connect(pointer.NewRequest());

  // Expect that the creator hasn't been called yet, because we haven't called |OnReady|.
  EXPECT_FALSE(!!deleter_from_create);
  EXPECT_FALSE(instance_deleted);

  under_test.OnReady();

  // Expect that the creator has been called.
  EXPECT_TRUE(!!deleter_from_create);
  EXPECT_FALSE(instance_deleted);

  deleter_from_create();

  EXPECT_TRUE(instance_deleted);

  deleter_from_create = nullptr;
  instance_deleted = false;
  pointer = nullptr;

  under_test.Connect(pointer.NewRequest());

  // Expect that the creator has been called.
  EXPECT_TRUE(!!deleter_from_create);
  EXPECT_FALSE(instance_deleted);

  deleter_from_create();

  EXPECT_TRUE(instance_deleted);
}

}  // namespace test
}  // namespace mdns
