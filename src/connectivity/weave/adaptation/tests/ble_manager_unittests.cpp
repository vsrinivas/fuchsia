// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/BLEManager.h>
#include "src/connectivity/weave/adaptation/ble_manager_impl.h"
// clang-format on

#include <gtest/gtest.h>
#include <fuchsia/bluetooth/gatt/cpp/fidl_test_base.h>
#include <fuchsia/bluetooth/le/cpp/fidl_test_base.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include "configuration_manager_delegate_impl.h"
#include "connectivity_manager_delegate_impl.h"
#include "thread_stack_manager_delegate_impl.h"
#include "weave_test_fixture.h"

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {
namespace {
using nl::Weave::DeviceLayer::ConnectivityManager;
using nl::Weave::DeviceLayer::Internal::BLEManager;
using nl::Weave::DeviceLayer::Internal::BLEManagerImpl;
using nl::Weave::DeviceLayer::Internal::BLEMgr;
}  // namespace

class FakeGATTLocalService : public fuchsia::bluetooth::gatt::testing::LocalService_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void RemoveService() override {
    binding_.Unbind();
    gatt_subscribe_confirmed_ = false;
  }

  void NotifyValue(uint64_t characteristic_id, std::string peer_id, std::vector<uint8_t> value,
                   bool confirm) override {
    gatt_subscribe_confirmed_ = true;
  }

  fidl::Binding<fuchsia::bluetooth::gatt::LocalService> binding_{this};
  bool gatt_subscribe_confirmed_{false};
};

class FakeGATTService : public fuchsia::bluetooth::gatt::testing::Server_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void PublishService(
      fuchsia::bluetooth::gatt::ServiceInfo info,
      fidl::InterfaceHandle<fuchsia::bluetooth::gatt::LocalServiceDelegate> delegate,
      fidl::InterfaceRequest<fuchsia::bluetooth::gatt::LocalService> service,
      PublishServiceCallback callback) override {
    ::fuchsia::bluetooth::Status resp;
    local_service_.binding_.Bind(std::move(service), gatt_server_dispatcher_);
    local_service_delegate_ = delegate.BindSync();

    callback(std::move(resp));
  }

  fidl::InterfaceRequestHandler<fuchsia::bluetooth::gatt::Server> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    gatt_server_dispatcher_ = dispatcher;
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::bluetooth::gatt::Server> request) {
      FX_LOGS(INFO) << "Binding to fake fuchsia::bluetooth::gatt::Server ";
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  fuchsia::bluetooth::gatt::ErrorCode WriteRequest() {
    // BTP connect request
    std::vector<uint8_t> value{0x6E, 0x6C, 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x5};
    fuchsia::bluetooth::gatt::ErrorCode out_status;
    local_service_delegate_->OnWriteValue(0, 0, value, &out_status);
    return out_status;
  }

  void OnCharacteristicConfiguration() {
    local_service_delegate_->OnCharacteristicConfiguration(1, "123456", false, true);
  }

  bool WeaveConnectionConfirmed() { return local_service_.gatt_subscribe_confirmed_; }

 private:
  fidl::Binding<fuchsia::bluetooth::gatt::Server> binding_{this};
  async_dispatcher_t* gatt_server_dispatcher_;
  FakeGATTLocalService local_service_;
  fidl::InterfaceHandle<class fuchsia::bluetooth::gatt::LocalServiceDelegate> delegate_;
  fuchsia::bluetooth::gatt::LocalServiceDelegateSyncPtr local_service_delegate_;
};

class FakeBLEPeripheral : public fuchsia::bluetooth::le::testing::Peripheral_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void StartAdvertising(fuchsia::bluetooth::le::AdvertisingParameters parameters,
                        fidl::InterfaceRequest<fuchsia::bluetooth::le::AdvertisingHandle> handle,
                        StartAdvertisingCallback callback) override {
    fuchsia::bluetooth::le::Peripheral_StartAdvertising_Response resp;
    fuchsia::bluetooth::le::Peripheral_StartAdvertising_Result result;
    result.set_response(std::move(resp));
    adv_handle_ = std::move(handle);
    FX_LOGS(INFO) << "Sending fake StartAdvertising response";
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<fuchsia::bluetooth::le::Peripheral> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    le_peripheral_dispatcher_ = dispatcher;
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::bluetooth::le::Peripheral> request) {
      FX_LOGS(INFO) << "Binding to fake fuchsia::bluetooth::le::Peripheral ";
      binding_.Bind(std::move(request), dispatcher);
    };
  }

 private:
  fidl::Binding<fuchsia::bluetooth::le::Peripheral> binding_{this};
  async_dispatcher_t* le_peripheral_dispatcher_;
  fidl::InterfaceRequest<fuchsia::bluetooth::le::AdvertisingHandle> adv_handle_;
};

class BLEManagerTest : public WeaveTestFixture {
 public:
  BLEManagerTest() {
    context_provider_.service_directory_provider()->AddService(
        fake_gatt_server_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_ble_peripheral_.GetHandler(dispatcher()));
  }

  void SetUp() {
    WeaveTestFixture::SetUp();
    WeaveTestFixture::RunFixtureLoop();

    PlatformMgrImpl().SetComponentContextForProcess(context_provider_.TakeContext());
    PlatformMgrImpl().SetDispatcher(event_loop_.dispatcher());
    PlatformMgrImpl().GetSystemLayer().Init(nullptr);

    ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
    ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
    ConnectivityMgrImpl().SetDelegate(std::make_unique<ConnectivityManagerDelegateImpl>());
    EXPECT_EQ(ConfigurationMgrImpl().IsWoBLEEnabled(), true);

    ble_mgr_ = std::make_unique<BLEManagerImpl>();
    InitBleMgr();
  }
  void TearDown() {
    event_loop_.Quit();
    WeaveTestFixture::StopFixtureLoop();
    WeaveTestFixture::TearDown();
  }

 protected:
  std::unique_ptr<BLEManagerImpl> ble_mgr_;

  void InitBleMgr() {
    EXPECT_EQ(ble_mgr_->_Init(), WEAVE_NO_ERROR);
    event_loop_.RunUntilIdle();
    EXPECT_EQ(GetBLEMgrServiceMode(), ConnectivityManager::kWoBLEServiceMode_Enabled);
    if (ConfigurationMgrImpl().IsWoBLEAdvertisementEnabled()) {
      EXPECT_EQ(IsBLEMgrAdvertising(), true);
    } else {
      EXPECT_EQ(IsBLEMgrAdvertising(), false);
    }
  }

  BLEManager::WoBLEServiceMode GetBLEMgrServiceMode() { return ble_mgr_->_GetWoBLEServiceMode(); }

  uint16_t IsBLEMgrAdvertising() { return ble_mgr_->_IsAdvertising(); }

  WEAVE_ERROR GetBLEMgrDeviceName(char* device_name, size_t device_name_size) {
    return ble_mgr_->_GetDeviceName(device_name, device_name_size);
  }

  WEAVE_ERROR SetBLEMgrDeviceName(const char* device_name) {
    return ble_mgr_->_SetDeviceName(device_name);
  }

  void SetWoBLEAdvertising(bool enabled) {
    EXPECT_EQ(ble_mgr_->_SetAdvertisingEnabled(enabled), WEAVE_NO_ERROR);
    event_loop_.RunUntilIdle();
  }

  void WeaveConnect() {
    EXPECT_EQ(fake_gatt_server_.WriteRequest(), fuchsia::bluetooth::gatt::ErrorCode::NO_ERROR);
    event_loop_.RunUntilIdle();
    EXPECT_EQ(fake_gatt_server_.WeaveConnectionConfirmed(), false);
    fake_gatt_server_.OnCharacteristicConfiguration();
    // Event loop will be idle and waiting for subscribe request(characteristic configuration)
    // on timer. So we need to wait until either subscribe request is received or timeout.
    event_loop_.Run(zx::time::infinite(), true /*once*/);

    // Stop fixture loop before waiting for FakeGATTLocalService::NotifyValue
    // on dispatcher().
    WeaveTestFixture::StopFixtureLoop();
    // Wait until FakeGATTLocalService::NotifyValue is called.
    RunLoopUntil([&]() {
      bool res = fake_gatt_server_.WeaveConnectionConfirmed();
      return res;
    });
    // Wait for FakeGATTLocalService::NotifyValue completed. Restart fixture loop.
    WeaveTestFixture::RunFixtureLoop();

    bool is_confirmed = fake_gatt_server_.WeaveConnectionConfirmed();
    EXPECT_EQ(is_confirmed, true);
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;

  FakeGATTService fake_gatt_server_;
  FakeBLEPeripheral fake_ble_peripheral_;
  async::Loop event_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

TEST_F(BLEManagerTest, SetAndGetDeviceName) {
  constexpr char kLargeDeviceName[] = "TOO_LARGE_DEVICE_NAME_FUCHSIA";
  constexpr char kDeviceName[] = "FUCHSIATEST";
  char read_value[kMaxDeviceNameLength + 1];
  EXPECT_EQ(SetBLEMgrDeviceName(kLargeDeviceName), WEAVE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(SetBLEMgrDeviceName(kDeviceName), WEAVE_NO_ERROR);
  EXPECT_EQ(GetBLEMgrDeviceName(read_value, 1), WEAVE_ERROR_BUFFER_TOO_SMALL);
  EXPECT_EQ(GetBLEMgrDeviceName(read_value, sizeof(read_value)), WEAVE_NO_ERROR);
  EXPECT_STREQ(kDeviceName, read_value);
}

TEST_F(BLEManagerTest, EnableAndDisableAdvertising) {
  // Disable Weave service advertising
  SetWoBLEAdvertising(false);
  EXPECT_EQ(IsBLEMgrAdvertising(), false);
  // Enable Weave service advertising
  SetWoBLEAdvertising(true);
  EXPECT_EQ(IsBLEMgrAdvertising(), true);
  // Re-enable Weave service advertising
  SetWoBLEAdvertising(true);
  EXPECT_EQ(IsBLEMgrAdvertising(), true);
}

TEST_F(BLEManagerTest, TestWeaveConnect) { WeaveConnect(); }

}  // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal
