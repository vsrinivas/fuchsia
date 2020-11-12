// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/connectivity/weave/weavestack/fidl/stack_impl.h"

#include <fuchsia/weave/cpp/fidl_test_base.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>

#include "generic_platform_manager_impl_fuchsia.ipp"
#include "configuration_manager_delegate_impl.h"
#include "connectivity_manager_delegate_impl.h"
#include "network_provisioning_server_delegate_impl.h"
#include "thread_stack_manager_delegate_impl.h"
// clang-format on

#include <unordered_map>
#include <gtest/gtest.h>

namespace weavestack {
namespace {
using fuchsia::weave::Host;
using fuchsia::weave::HostPort;
using fuchsia::weave::PairingState;
using fuchsia::weave::PairingStateWatcherPtr;
using fuchsia::weave::ResetConfigFlags;
using fuchsia::weave::Stack_GetQrCode_Result;
using fuchsia::weave::Stack_ResetConfig_Result;
using fuchsia::weave::SvcDirectoryWatcherPtr;

using nl::Weave::DeviceLayer::ConfigurationManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConfigurationMgrImpl;
using nl::Weave::DeviceLayer::ConnectivityManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConnectivityMgrImpl;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
using nl::Weave::DeviceLayer::ThreadStackManagerDelegateImpl;
using nl::Weave::DeviceLayer::ThreadStackMgrImpl;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningServerDelegateImpl;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningServerImpl;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningSvrImpl;
using nl::Weave::Profiles::DeviceControl::DeviceControlDelegate;
}  // namespace

class FakeWeaveFactoryDataManager : public fuchsia::weave::testing::FactoryDataManager_TestBase {
 public:
  FakeWeaveFactoryDataManager() : binding_(this) {}
  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void GetPairingCode(GetPairingCodeCallback callback) override {
    constexpr char device_pairing_code[] = "PAIRCODE123";
    fuchsia::weave::FactoryDataManager_GetPairingCode_Result result;
    fuchsia::weave::FactoryDataManager_GetPairingCode_Response response((::std::vector<uint8_t>(
        std::begin(device_pairing_code), std::end(device_pairing_code) - 1)));
    result.set_response(response);
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<fuchsia::weave::FactoryDataManager> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    dispatcher_ = dispatcher;
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::weave::FactoryDataManager> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

 private:
  fidl::Binding<fuchsia::weave::FactoryDataManager> binding_;
  async_dispatcher_t* dispatcher_;
};

class TestableStackImpl : public StackImpl {
 public:
  TestableStackImpl(sys::ComponentContext* context) : StackImpl(context) {}

  // Set the result for a future LookupHostPorts call
  void SetLookupResult(uint64_t endpoint_id, std::vector<HostPort> result) {
    map_[endpoint_id] = std::move(result);
  }

  // Set the result and expected argument to OnResetConfig
  void SetExpectedResetConfigCall(uint16_t argument, WEAVE_ERROR result) {
    expected_reset_config_flags_ = argument;
    reset_config_result_ = result;
    reset_config_was_called_ = false;
  }

  bool ResetConfigWasCalled() { return reset_config_was_called_; }

 private:
  class FakeDeviceControl : public DeviceControlDelegate {
   public:
    explicit FakeDeviceControl(TestableStackImpl* parent) : parent_(parent) {}

   private:
    WEAVE_ERROR OnResetConfig(uint16_t resetFlags) override {
      parent_->reset_config_was_called_ = true;
      if (resetFlags != parent_->expected_reset_config_flags_) {
        ADD_FAILURE() << "OnResetConfig recieved the incorrect flags";
      }
      return parent_->reset_config_result_;
    }

    // Following aren't used, only present to satisfy interface
    bool ShouldCloseConBeforeResetConfig(uint16_t resetFlags) override { return true; }
    WEAVE_ERROR OnFailSafeArmed(void) override { return WEAVE_NO_ERROR; }
    WEAVE_ERROR OnFailSafeDisarmed(void) override { return WEAVE_NO_ERROR; }
    void OnConnectionMonitorTimeout(uint64_t peerNodeId, nl::Inet::IPAddress peerAddr) override {}
    void OnRemotePassiveRendezvousStarted(void) override {}
    void OnRemotePassiveRendezvousDone(void) override {}
    WEAVE_ERROR WillStartRemotePassiveRendezvous(void) override { return WEAVE_NO_ERROR; }
    void WillCloseRemotePassiveRendezvous(void) override {}
    bool IsResetAllowed(uint16_t resetFlags) override { return true; }
    WEAVE_ERROR OnSystemTestStarted(uint32_t profileId, uint32_t testId) override {
      return WEAVE_NO_ERROR;
    }
    WEAVE_ERROR OnSystemTestStopped(void) override { return WEAVE_NO_ERROR; }
    void EnforceAccessControl(nl::Weave::ExchangeContext* ec, uint32_t msgProfileId,
                              uint8_t msgType, const nl::Weave::WeaveMessageInfo* msgInfo,
                              AccessControlResult& result) override {}
    bool IsPairedToAccount() const override { return false; }

    TestableStackImpl* parent_;
  };

  zx_status_t LookupHostPorts(uint64_t endpoint_id, std::vector<HostPort>* host_ports) override {
    // Lookup in map
    auto lookup = map_.find(endpoint_id);
    if (lookup == map_.end()) {
      return ZX_ERR_INVALID_ARGS;
    }

    // Copy results to host_ports
    for (const HostPort& host_port : lookup->second) {
      HostPort temp;
      zx_status_t status = host_port.Clone(&temp);
      if (status != ZX_OK) {
        ADD_FAILURE() << "host_port.Clone(...) should not fail";
        return status;
      }
    }

    return ZX_OK;
  }

  nl::Weave::Profiles::DeviceControl::DeviceControlDelegate& GetDeviceControl() override {
    return fake_device_control_;
  }

  // Faked service directory
  std::unordered_map<uint64_t, std::vector<HostPort>> map_;
  // Faked device control
  FakeDeviceControl fake_device_control_{this};
  // Result to return on "successful" faked ResetConfig
  WEAVE_ERROR reset_config_result_;
  // Expected argument to ResetConfig
  uint16_t expected_reset_config_flags_;
  // Mark if the OnResetConfig device control call was made
  bool reset_config_was_called_;
};

class StackImplTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();

    // Set up FactoryDataManager
    provider_.service_directory_provider()->AddService(
        fake_weave_factory_data_manager_.GetHandler(dispatcher()));

    // Initialize the weave stack
    ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
    ConnectivityMgrImpl().SetDelegate(std::make_unique<ConnectivityManagerDelegateImpl>());
    NetworkProvisioningSvrImpl().SetDelegate(
        std::make_unique<NetworkProvisioningServerDelegateImpl>());
    ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
    ASSERT_EQ(PlatformMgrImpl().InitWeaveStack(), WEAVE_NO_ERROR);

    // Set up StackImpl
    stack_impl_ = std::make_unique<TestableStackImpl>(provider_.context());
    stack_impl_->Init();

    // Connect to the UUT
    provider_.ConnectToPublicService(weave_stack_.NewRequest());
    ASSERT_TRUE(weave_stack_.is_bound());
  }

  void TearDown() override {
    // Shut down the weave stack
    PlatformMgrImpl().ShutdownWeaveStack();
    TestLoopFixture::TearDown();
  }

 protected:
  fuchsia::weave::StackPtr weave_stack_;
  std::unique_ptr<TestableStackImpl> stack_impl_;

 private:
  FakeWeaveFactoryDataManager fake_weave_factory_data_manager_;
  sys::testing::ComponentContextProvider provider_;
};

// Test Cases ------------------------------------------------------------------

TEST_F(StackImplTest, GetPairingStateWatcher) {
  PairingStateWatcherPtr watcher;
  bool callback_triggered = false;

  // Get watcher
  weave_stack_->GetPairingStateWatcher(watcher.NewRequest());
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher.is_bound());

  // Get first value
  watcher->WatchPairingState([&](PairingState result) {
    EXPECT_FALSE(result.IsEmpty());
    callback_triggered = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_triggered) << "Callback not triggered for first value.";

  // Try hanging get
  callback_triggered = false;
  watcher->WatchPairingState([&](PairingState result) {
    EXPECT_FALSE(result.IsEmpty());
    callback_triggered = true;
  });
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered) << "Callback triggered on hanging get.";

  // Notify and expect callback
  stack_impl_->NotifyPairingState();
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_triggered) << "Callback not triggered after notify.";
}

TEST_F(StackImplTest, GetPairingStateWatcherDoubleCall) {
  PairingStateWatcherPtr watcher;
  bool callback_triggered = false;
  bool error_handler_triggered = false;
  zx_status_t error_status = ZX_OK;

  // Set error handler on watcher
  watcher.set_error_handler([&](zx_status_t status) {
    error_handler_triggered = true;
    error_status = status;
  });

  // Get watcher
  weave_stack_->GetPairingStateWatcher(watcher.NewRequest());
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher.is_bound());

  // Get first value
  watcher->WatchPairingState([&](PairingState result) {
    EXPECT_FALSE(result.IsEmpty());
    callback_triggered = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_triggered) << "Callback not triggered for first value.";

  // Try hanging get
  callback_triggered = false;
  watcher->WatchPairingState([&](PairingState result) {
    EXPECT_FALSE(result.IsEmpty());
    callback_triggered = true;
  });
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered) << "Callback triggered on hanging get.";

  // Try second get while still waiting on first
  callback_triggered = false;
  watcher->WatchPairingState([&](PairingState result) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered) << "Callback triggered on hanging get.";
  EXPECT_TRUE(error_handler_triggered);
  EXPECT_EQ(error_status, ZX_ERR_BAD_STATE);
}

TEST_F(StackImplTest, GetSvcDirectoryWatcher) {
  constexpr uint64_t kFakeEndpointId = 1337;
  SvcDirectoryWatcherPtr watcher;
  bool callback_triggered = false;

  // Set test data
  std::vector<HostPort> test_data;
  test_data.emplace_back(HostPort{.host = Host::WithHostname("test"), .port = 42});
  stack_impl_->SetLookupResult(kFakeEndpointId, std::move(test_data));

  // Get watcher
  weave_stack_->GetSvcDirectoryWatcher(kFakeEndpointId, watcher.NewRequest());
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher.is_bound());

  // Get first value
  watcher->WatchServiceDirectory([&](std::vector<HostPort> result) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_triggered) << "Callback not triggered for first value.";

  // Try hanging get
  callback_triggered = false;
  watcher->WatchServiceDirectory([&](std::vector<HostPort> result) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered) << "Callback triggered on hanging get.";

  // Notify and expect callback
  stack_impl_->NotifySvcDirectory();
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_triggered) << "Callback not triggered after notify.";
}

TEST_F(StackImplTest, GetSvcDirectoryWatcherDoubleCall) {
  constexpr uint64_t kFakeEndpointId = 1337;
  SvcDirectoryWatcherPtr watcher;
  bool callback_triggered = false;
  bool error_handler_triggered = false;
  zx_status_t error_status = ZX_OK;

  // Set test data
  std::vector<HostPort> test_data;
  test_data.emplace_back(HostPort{.host = Host::WithHostname("test"), .port = 42});
  stack_impl_->SetLookupResult(kFakeEndpointId, std::move(test_data));

  // Set error handler on watcher
  watcher.set_error_handler([&](zx_status_t status) {
    error_handler_triggered = true;
    error_status = status;
  });

  // Get watcher
  weave_stack_->GetSvcDirectoryWatcher(kFakeEndpointId, watcher.NewRequest());
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher.is_bound());

  // Get first value
  watcher->WatchServiceDirectory([&](std::vector<HostPort> result) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_triggered) << "Callback not triggered for first value.";

  // Try hanging get
  callback_triggered = false;
  watcher->WatchServiceDirectory([&](std::vector<HostPort> result) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered) << "Callback triggered on hanging get.";

  // Try second get while still waiting on first
  callback_triggered = false;
  watcher->WatchServiceDirectory([&](std::vector<HostPort> result) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered) << "Callback triggered on hanging get.";
  EXPECT_TRUE(error_handler_triggered);
  EXPECT_EQ(error_status, ZX_ERR_BAD_STATE);
}

TEST_F(StackImplTest, GetSvcDirectoryWatcherInvalidEndpoint) {
  constexpr uint64_t kFakeEndpointId = 42;
  SvcDirectoryWatcherPtr watcher;
  bool callback_triggered = false;
  bool error_handler_triggered = false;
  zx_status_t error_status = ZX_OK;

  // Set error handler on watcher
  watcher.set_error_handler([&](zx_status_t status) {
    error_handler_triggered = true;
    error_status = status;
  });

  // Get watcher
  weave_stack_->GetSvcDirectoryWatcher(kFakeEndpointId, watcher.NewRequest());
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher.is_bound());
  EXPECT_FALSE(error_handler_triggered);
  EXPECT_EQ(error_status, ZX_OK);

  // Get first value
  watcher->WatchServiceDirectory([&](std::vector<HostPort> result) { callback_triggered = true; });
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_triggered) << "Callback triggered on invalid value.";
  EXPECT_TRUE(error_handler_triggered);
  EXPECT_EQ(error_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(StackImplTest, GetQrCode) {
  Stack_GetQrCode_Result result;

  nl::Weave::FabricState.PairingCode = "ABCDEF";
  weave_stack_->GetQrCode([&](Stack_GetQrCode_Result r) { result = std::move(r); });
  RunLoopUntilIdle();

  EXPECT_FALSE(result.is_err()) << "Failed with err: " << static_cast<uint32_t>(result.err());
  EXPECT_TRUE(result.is_response());
  EXPECT_FALSE(result.response().qr_code.data.empty());
}

TEST_F(StackImplTest, ResetConfig) {
  constexpr ResetConfigFlags kResetConfigFlags =
      ResetConfigFlags::NETWORK_CONFIG | ResetConfigFlags::FABRIC_CONFIG;
  Stack_ResetConfig_Result result;

  stack_impl_->SetExpectedResetConfigCall(static_cast<uint16_t>(kResetConfigFlags), WEAVE_NO_ERROR);

  weave_stack_->ResetConfig(kResetConfigFlags,
                            [&](Stack_ResetConfig_Result r) { result = std::move(r); });
  RunLoopUntilIdle();

  EXPECT_TRUE(stack_impl_->ResetConfigWasCalled());
  EXPECT_FALSE(result.is_err());
}

}  // namespace weavestack
