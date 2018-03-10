// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_service_manager.h"

#include <vector>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/testing/test_base.h"
#include "lib/fxl/macros.h"

#include "fake_client.h"

namespace btlib {
namespace gatt {
namespace internal {
namespace {

using common::HostError;

constexpr common::UUID kTestServiceUuid1((uint16_t)0xbeef);
constexpr common::UUID kTestServiceUuid2((uint16_t)0xcafe);

class GATT_RemoteServiceManagerTest : public ::btlib::testing::TestBase {
 public:
  GATT_RemoteServiceManagerTest() = default;
  ~GATT_RemoteServiceManagerTest() override = default;

 protected:
  void SetUp() override {
    auto client =
        std::make_unique<testing::FakeClient>(message_loop()->async());
    fake_client_ = client.get();

    mgr_ = std::make_unique<RemoteServiceManager>(std::move(client),
                                                  message_loop()->async());
  }

  void TearDown() override { mgr_ = nullptr; }

  RemoteServiceManager* mgr() const { return mgr_.get(); }
  testing::FakeClient* fake_client() const { return fake_client_; }

 private:
  std::unique_ptr<RemoteServiceManager> mgr_;

  // The memory is owned by |mgr_|.
  testing::FakeClient* fake_client_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GATT_RemoteServiceManagerTest);
};

TEST_F(GATT_RemoteServiceManagerTest, InitializeNoServices) {
  std::vector<fbl::RefPtr<RemoteService>> services;
  mgr()->set_service_watcher(
      [&services](auto svc) { services.push_back(svc); });

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status cb_res) { status = cb_res; });

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_TRUE(services.empty());

  mgr()->ListServices(std::vector<common::UUID>(),
                      [&services](auto status, ServiceList cb_services) {
                        services = std::move(cb_services);
                      });
  EXPECT_TRUE(services.empty());
}

TEST_F(GATT_RemoteServiceManagerTest, Initialize) {
  ServiceData svc1(1, 1, kTestServiceUuid1);
  ServiceData svc2(2, 2, kTestServiceUuid2);
  std::vector<ServiceData> fake_services{{svc1, svc2}};
  fake_client()->set_primary_services(std::move(fake_services));

  ServiceList services;
  mgr()->set_service_watcher(
      [&services](auto svc) { services.push_back(svc); });

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status cb_res) { status = cb_res; });

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(2u, services.size());
  EXPECT_EQ(svc1.range_start, services[0]->handle());
  EXPECT_EQ(svc2.range_start, services[1]->handle());
  EXPECT_EQ(svc1.type, services[0]->uuid());
  EXPECT_EQ(svc2.type, services[1]->uuid());
}

TEST_F(GATT_RemoteServiceManagerTest, InitializeFailure) {
  fake_client()->set_service_discovery_status(
      att::Status(att::ErrorCode::kRequestNotSupported));

  ServiceList watcher_services;
  mgr()->set_service_watcher(
      [&watcher_services](auto svc) { watcher_services.push_back(svc); });

  ServiceList services;
  mgr()->ListServices(std::vector<common::UUID>(),
                      [&services](auto status, ServiceList cb_services) {
                        services = std::move(cb_services);
                      });
  ASSERT_TRUE(services.empty());

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status cb_res) { status = cb_res; });

  RunUntilIdle();

  EXPECT_FALSE(status);
  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(att::ErrorCode::kRequestNotSupported, status.protocol_error());
  EXPECT_TRUE(services.empty());
  EXPECT_TRUE(watcher_services.empty());
}

TEST_F(GATT_RemoteServiceManagerTest, ListServicesBeforeInit) {
  ServiceData svc(1, 1, kTestServiceUuid1);
  std::vector<ServiceData> fake_services{{svc}};
  fake_client()->set_primary_services(std::move(fake_services));

  ServiceList services;
  mgr()->ListServices(std::vector<common::UUID>(),
                      [&services](auto status, ServiceList cb_services) {
                        services = std::move(cb_services);
                      });
  EXPECT_TRUE(services.empty());

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status cb_res) { status = cb_res; });

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(1u, services.size());
  EXPECT_EQ(svc.range_start, services[0]->handle());
  EXPECT_EQ(svc.type, services[0]->uuid());
}

TEST_F(GATT_RemoteServiceManagerTest, ListServicesAfterInit) {
  ServiceData svc(1, 1, kTestServiceUuid1);
  std::vector<ServiceData> fake_services{{svc}};
  fake_client()->set_primary_services(std::move(fake_services));

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status cb_res) { status = cb_res; });

  RunUntilIdle();

  ASSERT_TRUE(status);

  ServiceList services;
  mgr()->ListServices(std::vector<common::UUID>(),
                      [&services](auto status, ServiceList cb_services) {
                        services = std::move(cb_services);
                      });
  EXPECT_EQ(1u, services.size());
  EXPECT_EQ(svc.range_start, services[0]->handle());
  EXPECT_EQ(svc.type, services[0]->uuid());
}

TEST_F(GATT_RemoteServiceManagerTest, ListServicesByUuid) {
  std::vector<common::UUID> uuids{kTestServiceUuid1};

  ServiceData svc1(1, 1, kTestServiceUuid1);
  ServiceData svc2(2, 2, kTestServiceUuid2);
  std::vector<ServiceData> fake_services{{svc1, svc2}};
  fake_client()->set_primary_services(std::move(fake_services));

  att::Status list_services_status;
  ServiceList services;
  mgr()->set_service_watcher(
      [&services](auto svc) { services.push_back(svc); });
  mgr()->ListServices(std::move(uuids),
                      [&](auto cb_status, ServiceList cb_services) {
                        list_services_status = cb_status;
                        services = std::move(cb_services);
                      });
  ASSERT_TRUE(services.empty());

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status cb_res) { status = cb_res; });

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_TRUE(list_services_status);
  EXPECT_EQ(1u, services.size());
  EXPECT_EQ(svc1.range_start, services[0]->handle());
  EXPECT_EQ(svc1.type, services[0]->uuid());
}

}  // namespace
}  // namespace internal
}  // namespace gatt
}  // namespace btlib
