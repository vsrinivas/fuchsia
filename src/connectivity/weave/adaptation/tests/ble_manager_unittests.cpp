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

#include "weave_test_fixture.h"

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {
namespace {
using nl::Weave::DeviceLayer::ConnectivityManager;
using nl::Weave::DeviceLayer::Internal::BLEManager;
using nl::Weave::DeviceLayer::Internal::BLEManagerImpl;
using nl::Weave::DeviceLayer::Internal::BLEMgr;
}  // namespace

class FakeGATTService : public fuchsia::bluetooth::gatt::testing::Server_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void PublishService(
      fuchsia::bluetooth::gatt::ServiceInfo info,
      ::fidl::InterfaceHandle<class fuchsia::bluetooth::gatt::LocalServiceDelegate> delegate,
      ::fidl::InterfaceRequest<fuchsia::bluetooth::gatt::LocalService> service,
      PublishServiceCallback callback) override {
    ::fuchsia::bluetooth::Status resp;
    FX_LOGS(INFO) << "Sending fake PublishService response";
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

 private:
  fidl::Binding<fuchsia::bluetooth::gatt::Server> binding_{this};
  async_dispatcher_t* gatt_server_dispatcher_;
};

class FakeBLEPeripheral : public fuchsia::bluetooth::le::testing::Peripheral_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void StartAdvertising(fuchsia::bluetooth::le::AdvertisingParameters parameters,
                        ::fidl::InterfaceRequest<fuchsia::bluetooth::le::AdvertisingHandle> handle,
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
  ::fidl::InterfaceRequest<fuchsia::bluetooth::le::AdvertisingHandle> adv_handle_;
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
    ble_mgr_ = std::make_unique<BLEManagerImpl>();
    EXPECT_EQ(ConfigurationMgrImpl().IsWOBLEEnabled(), true);
    InitBleMgr();
  }
  void TearDown() {
    WeaveTestFixture::StopFixtureLoop();
    WeaveTestFixture::TearDown();
  }

 protected:
  std::unique_ptr<BLEManagerImpl> ble_mgr_;

  void InitBleMgr() {
    EXPECT_EQ(ble_mgr_->_Init(), WEAVE_NO_ERROR);
    event_loop_.RunUntilIdle();
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

 private:
  sys::testing::ComponentContextProvider context_provider_;

  FakeGATTService fake_gatt_server_;
  FakeBLEPeripheral fake_ble_peripheral_;
  async::Loop event_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

TEST_F(BLEManagerTest, Init) {
  EXPECT_EQ(GetBLEMgrServiceMode(), ConnectivityManager::kWoBLEServiceMode_Enabled);
  EXPECT_EQ(IsBLEMgrAdvertising(), true);
}

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
  EXPECT_EQ(GetBLEMgrServiceMode(), ConnectivityManager::kWoBLEServiceMode_Enabled);
  EXPECT_EQ(IsBLEMgrAdvertising(), true);
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

}  // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal
