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

// This must be in the correct namespace for it to be visible to EXPECT_EQ.
static bool operator==(const CharacteristicData& chrc1,
                       const CharacteristicData& chrc2) {
  return chrc1.properties == chrc2.properties && chrc1.handle == chrc2.handle &&
         chrc1.value_handle == chrc2.value_handle && chrc1.type == chrc2.type;
}

namespace internal {
namespace {

using common::HostError;

constexpr common::UUID kTestServiceUuid1((uint16_t)0xbeef);
constexpr common::UUID kTestServiceUuid2((uint16_t)0xcafe);
constexpr common::UUID kTestUuid3((uint16_t)0xfefe);
constexpr common::UUID kTestUuid4((uint16_t)0xefef);

using common::HostError;

void NopStatusCallback(att::Status) {}

class GATT_RemoteServiceManagerTest : public ::btlib::testing::TestBase {
 public:
  GATT_RemoteServiceManagerTest() = default;
  ~GATT_RemoteServiceManagerTest() override = default;

 protected:
  void SetUp() override {
    auto client =
        std::make_unique<testing::FakeClient>(dispatcher());
    fake_client_ = client.get();

    mgr_ = std::make_unique<RemoteServiceManager>(std::move(client),
                                                  dispatcher());
  }

  void TearDown() override { mgr_ = nullptr; }

  // Initializes a RemoteService based on |data|.
  fbl::RefPtr<RemoteService> SetUpFakeService(const ServiceData& data) {
    std::vector<ServiceData> fake_services{{data}};
    fake_client()->set_primary_services(std::move(fake_services));

    mgr()->Initialize(NopStatusCallback);

    ServiceList services;
    mgr()->ListServices(std::vector<common::UUID>(),
                        [&services](auto status, ServiceList cb_services) {
                          services = std::move(cb_services);
                        });

    RunUntilIdle();

    FXL_DCHECK(services.size() == 1u);
    return services[0];
  }

  // Discover the characteristics of |service| based on the given |fake_data|.
  void SetupCharacteristics(fbl::RefPtr<RemoteService> service,
                            std::vector<CharacteristicData> fake_data) {
    FXL_DCHECK(service);

    fake_client()->set_characteristics(std::move(fake_data));
    fake_client()->set_characteristic_discovery_status(att::Status());

    RemoteCharacteristicList characteristics;
    service->DiscoverCharacteristics([](auto, const auto&) {});

    RunUntilIdle();
  }

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
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

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
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

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
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

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
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

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
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

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
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_TRUE(list_services_status);
  EXPECT_EQ(1u, services.size());
  EXPECT_EQ(svc1.range_start, services[0]->handle());
  EXPECT_EQ(svc1.type, services[0]->uuid());
}

TEST_F(GATT_RemoteServiceManagerTest, DiscoverCharacteristicsAfterShutDown) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  service->ShutDown();

  att::Status status;
  size_t chrcs_size;
  service->DiscoverCharacteristics([&](auto cb_status, const auto& chrcs) {
    status = cb_status;
    chrcs_size = chrcs.size();
  });

  RunUntilIdle();
  EXPECT_FALSE(status);
  EXPECT_EQ(HostError::kFailed, status.error());
  EXPECT_EQ(0u, chrcs_size);
  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  EXPECT_FALSE(service->IsDiscovered());
}

TEST_F(GATT_RemoteServiceManagerTest, DiscoverCharacteristicsSuccess) {
  ServiceData data(1, 5, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData fake_chrc1(0, 2, 3, kTestUuid3);
  CharacteristicData fake_chrc2(0, 4, 5, kTestUuid4);
  std::vector<CharacteristicData> fake_chrcs{{fake_chrc1, fake_chrc2}};
  fake_client()->set_characteristics(std::move(fake_chrcs));

  att::Status status1(HostError::kFailed);
  service->DiscoverCharacteristics([&](auto cb_status, const auto& chrcs) {
    status1 = cb_status;
    EXPECT_EQ(2u, chrcs.size());

    EXPECT_EQ(0u, chrcs[0].id());
    EXPECT_EQ(1u, chrcs[1].id());

    EXPECT_EQ(fake_chrc1, chrcs[0].info());
    EXPECT_EQ(fake_chrc2, chrcs[1].info());
  });

  // Queue a second request.
  att::Status status2(HostError::kFailed);
  RemoteCharacteristicList chrcs2;
  service->DiscoverCharacteristics([&](auto cb_status, const auto& chrcs) {
    status2 = cb_status;
    EXPECT_EQ(2u, chrcs.size());

    EXPECT_EQ(0u, chrcs[0].id());
    EXPECT_EQ(1u, chrcs[1].id());

    EXPECT_EQ(fake_chrc1, chrcs[0].info());
    EXPECT_EQ(fake_chrc2, chrcs[1].info());
  });

  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  RunUntilIdle();

  // Only one ATT request should have been made.
  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());

  EXPECT_TRUE(service->IsDiscovered());
  EXPECT_TRUE(status1);
  EXPECT_TRUE(status2);
  EXPECT_EQ(data.range_start,
            fake_client()->last_chrc_discovery_start_handle());
  EXPECT_EQ(data.range_end, fake_client()->last_chrc_discovery_end_handle());

  // Request discovery again. This should succeed without an ATT request.
  status1 = att::Status(HostError::kFailed);
  service->DiscoverCharacteristics(
      [&status1](auto cb_status, const auto&) { status1 = cb_status; });

  RunUntilIdle();

  EXPECT_TRUE(status1);
  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());
  EXPECT_TRUE(service->IsDiscovered());
}

TEST_F(GATT_RemoteServiceManagerTest, DiscoverCharacteristicsError) {
  ServiceData data(1, 5, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData chrc1(0, 2, 3, kTestUuid3);
  CharacteristicData chrc2(0, 4, 5, kTestUuid4);
  std::vector<CharacteristicData> fake_chrcs{{chrc1, chrc2}};
  fake_client()->set_characteristics(std::move(fake_chrcs));

  fake_client()->set_characteristic_discovery_status(
      att::Status(HostError::kNotSupported));

  att::Status status1;
  RemoteCharacteristicList chrcs1;
  service->DiscoverCharacteristics([&](auto cb_status, const auto& chrcs) {
    status1 = cb_status;
    EXPECT_TRUE(chrcs.empty());
  });

  // Queue a second request.
  att::Status status2;
  RemoteCharacteristicList chrcs2;
  service->DiscoverCharacteristics([&](auto cb_status, const auto& chrcs) {
    status2 = cb_status;
    EXPECT_TRUE(chrcs.empty());
  });

  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  RunUntilIdle();

  // Onle one request should have been made.
  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());

  EXPECT_FALSE(service->IsDiscovered());
  EXPECT_FALSE(status1);
  EXPECT_FALSE(status2);
  EXPECT_EQ(HostError::kNotSupported, status1.error());
  EXPECT_EQ(HostError::kNotSupported, status2.error());
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharAfterShutDown) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  service->ShutDown();

  att::Status status;
  service->WriteCharacteristic(0, std::vector<uint8_t>(),
                               [&](auto cb_status) { status = cb_status; });

  RunUntilIdle();

  EXPECT_EQ(HostError::kFailed, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharWhileNotReady) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  att::Status status;
  service->WriteCharacteristic(0, std::vector<uint8_t>(),
                               [&](auto cb_status) { status = cb_status; });

  RunUntilIdle();

  EXPECT_EQ(HostError::kNotReady, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharNotFound) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);
  SetupCharacteristics(service, std::vector<CharacteristicData>());

  att::Status status;
  service->WriteCharacteristic(0, std::vector<uint8_t>(),
                               [&](auto cb_status) { status = cb_status; });

  RunUntilIdle();

  EXPECT_EQ(HostError::kNotFound, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharNotSupported) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  // No "write" property set.
  CharacteristicData chr(0, 2, 3, kTestUuid3);
  SetupCharacteristics(service, {{chr}});

  att::Status status;
  service->WriteCharacteristic(0, std::vector<uint8_t>(),
                               [&](auto cb_status) { status = cb_status; });

  RunUntilIdle();

  EXPECT_EQ(HostError::kNotSupported, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharSendsWriteRequest) {
  constexpr att::Handle kValueHandle = 0x00fe;
  const std::vector<uint8_t> kValue{{'t', 'e', 's', 't'}};
  constexpr att::Status kStatus(att::ErrorCode::kWriteNotPermitted);

  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData chr(Property::kWrite, 2, kValueHandle, kTestUuid3);
  SetupCharacteristics(service, {{chr}});

  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_callback) {
        EXPECT_EQ(kValueHandle, handle);
        EXPECT_TRUE(std::equal(kValue.begin(), kValue.end(), value.begin(),
                               value.end()));
        status_callback(kStatus);
      });

  att::Status status;
  service->WriteCharacteristic(0, kValue,
                               [&](auto cb_status) { status = cb_status; });

  RunUntilIdle();

  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(kStatus, status);
}

}  // namespace
}  // namespace internal
}  // namespace gatt
}  // namespace btlib
