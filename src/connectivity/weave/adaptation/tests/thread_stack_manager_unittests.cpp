// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/lowpan/device/cpp/fidl_test_base.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
// clang-format on

#include "src/connectivity/weave/adaptation/thread_stack_manager_delegate_impl.h"
#include "weave_test_fixture.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {
namespace testing {

namespace {
using fuchsia::lowpan::ConnectivityState;
using fuchsia::lowpan::Credential;
using fuchsia::lowpan::Identity;
using fuchsia::lowpan::ProvisioningParams;
using fuchsia::lowpan::Role;
using fuchsia::lowpan::device::Device;
using fuchsia::lowpan::device::DeviceExtra;
using fuchsia::lowpan::device::DeviceState;
using fuchsia::lowpan::device::Lookup;
using fuchsia::lowpan::device::Lookup_LookupDevice_Response;
using fuchsia::lowpan::device::Lookup_LookupDevice_Result;
using fuchsia::lowpan::device::Protocols;
using fuchsia::lowpan::device::ServiceError;

using ThreadDeviceType = ConnectivityManager::ThreadDeviceType;
using nl::Weave::DeviceLayer::ThreadStackManagerDelegateImpl;

const char kFakeInterfaceName[] = "fake0";

// Helper to format bytes in log messages.
std::string FormatBytes(const std::vector<uint8_t>& bytes) {
  std::stringstream ss;
  bool first = true;

  ss << "[";
  for (auto byte : bytes) {
    if (!first) {
      ss << ", ";
    }
    first = false;
    ss << "0x" << std::hex << +byte;
  }
  ss << "]";

  return ss.str();
}

}  // namespace

class FakeLowpanDevice final : public fuchsia::lowpan::device::testing::Device_TestBase,
                               public fuchsia::lowpan::device::testing::DeviceExtra_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  // Fidl interfaces.

  void GetCredential(GetCredentialCallback callback) override {
    std::unique_ptr<Credential> cloned_credential = std::make_unique<Credential>();

    ASSERT_EQ(credential_.Clone(cloned_credential.get()), ZX_OK);
    callback(std::move(cloned_credential));
  }

  void GetSupportedNetworkTypes(GetSupportedNetworkTypesCallback callback) override {
    callback({fuchsia::lowpan::NET_TYPE_THREAD_1_X});
  }

  void LeaveNetwork(LeaveNetworkCallback callback) override {
    identity_ = Identity();
    credential_ = Credential();

    // Transition state.
    switch (connectivity_state_) {
      case ConnectivityState::ATTACHING:
      case ConnectivityState::ATTACHED:
      case ConnectivityState::ISOLATED:
        connectivity_state_ = ConnectivityState::OFFLINE;
        break;
      case ConnectivityState::READY:
        connectivity_state_ = ConnectivityState::INACTIVE;
        break;
      default:
        // Do nothing, device was not on network.
        break;
    }

    callback();
  }

  void ProvisionNetwork(ProvisioningParams params, ProvisionNetworkCallback callback) override {
    identity_ = std::move(params.identity);
    if (params.credential) {
      credential_ = std::move(*params.credential);
    }

    // Transition state.
    switch (connectivity_state_) {
      case ConnectivityState::INACTIVE:
        connectivity_state_ = ConnectivityState::READY;
        break;
      case ConnectivityState::OFFLINE:
      case ConnectivityState::COMMISSIONING:
        connectivity_state_ = ConnectivityState::ATTACHED;
        break;
      default:
        // Do nothing, device is already provisioned.
        break;
    }

    callback();
  }

  void SetActive(bool active, SetActiveCallback callback) override {
    // Transition state.
    if (active) {
      switch (connectivity_state_) {
        case ConnectivityState::INACTIVE:
          connectivity_state_ = ConnectivityState::OFFLINE;
          break;
        case ConnectivityState::READY:
          connectivity_state_ = ConnectivityState::ATTACHED;
          break;
        default:
          // Do nothing, device is already active.
          break;
      }
    } else {
      switch (connectivity_state_) {
        case ConnectivityState::OFFLINE:
        case ConnectivityState::COMMISSIONING:
          connectivity_state_ = ConnectivityState::INACTIVE;
          break;
        case ConnectivityState::ATTACHING:
        case ConnectivityState::ATTACHED:
        case ConnectivityState::ISOLATED:
          connectivity_state_ = ConnectivityState::READY;
          break;
        default:
          // Do nothing, device is already inactive.
          break;
      }
    }

    callback();
  }

  void WatchDeviceState(WatchDeviceStateCallback callback) override {
    callback(std::move(DeviceState().set_role(role_).set_connectivity_state(connectivity_state_)));
  }

  void WatchIdentity(WatchIdentityCallback callback) override {
    Identity cloned_identity;

    ASSERT_EQ(identity_.Clone(&cloned_identity), ZX_OK);
    callback(std::move(cloned_identity));
  }

  // Accessors/mutators for testing.

  void set_dispatcher(async_dispatcher_t* dispatcher) { dispatcher_ = dispatcher; }

  ConnectivityState connectivity_state() { return connectivity_state_; }

  Credential& credential() { return credential_; }

  Identity& identity() { return identity_; }

  FakeLowpanDevice& set_connectivity_state(ConnectivityState state) {
    connectivity_state_ = state;
    return *this;
  }

  Role role() { return role_; }

  FakeLowpanDevice& set_role(Role role) {
    role_ = role;
    return *this;
  }

 private:
  WatchDeviceStateCallback watch_device_state_callback_;
  async_dispatcher_t* dispatcher_;

  ConnectivityState connectivity_state_{ConnectivityState::INACTIVE};
  Credential credential_;
  Identity identity_;
  Role role_{Role::DETACHED};
};

class FakeLowpanLookup final : public fuchsia::lowpan::device::testing::Lookup_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  void GetDevices(GetDevicesCallback callback) override { callback({kFakeInterfaceName}); }

  void LookupDevice(std::string name, Protocols protocols, LookupDeviceCallback callback) override {
    Lookup_LookupDevice_Result result;
    if (name != kFakeInterfaceName) {
      result.set_err(ServiceError::DEVICE_NOT_FOUND);
      callback(std::move(result));
      return;
    }

    Lookup_LookupDevice_Response response;
    if (protocols.has_device()) {
      device_bindings_.AddBinding(&device_, std::move(*protocols.mutable_device()), dispatcher_);
    }

    if (protocols.has_device_extra()) {
      device_extra_bindings_.AddBinding(&device_, std::move(*protocols.mutable_device_extra()),
                                        dispatcher_);
    }

    result.set_response(response);
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<Lookup> GetHandler(async_dispatcher_t* dispatcher) {
    dispatcher_ = dispatcher;
    device_.set_dispatcher(dispatcher);
    return [this](fidl::InterfaceRequest<Lookup> request) {
      binding_.Bind(std::move(request), dispatcher_);
    };
  }

  FakeLowpanDevice& device() { return device_; }

 private:
  FakeLowpanDevice device_;
  fidl::BindingSet<Device> device_bindings_;
  fidl::BindingSet<DeviceExtra> device_extra_bindings_;
  async_dispatcher_t* dispatcher_;
  fidl::Binding<Lookup> binding_{this};
};

class ThreadStackManagerTest : public WeaveTestFixture {
 public:
  ThreadStackManagerTest() {
    context_provider_.service_directory_provider()->AddService(
        fake_lookup_.GetHandler(dispatcher()));
  }

  void SetUp() override {
    WeaveTestFixture::SetUp();
    PlatformMgrImpl().SetComponentContextForProcess(context_provider_.TakeContext());
    RunFixtureLoop();
    ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
    ASSERT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_NO_ERROR);
  }

  void TearDown() override {
    StopFixtureLoop();
    WeaveTestFixture::TearDown();
  }

 protected:
  FakeLowpanLookup fake_lookup_;

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<sys::ComponentContext> context_;
};

TEST_F(ThreadStackManagerTest, IsEnabled) {
  // Confirm initial INACTIVE => false.
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadEnabled());
  // Set to active but offline and confirm.
  fake_lookup_.device().set_connectivity_state(ConnectivityState::OFFLINE);
  EXPECT_TRUE(ThreadStackMgrImpl()._IsThreadEnabled());
  // Set to ready but inactive and confirm.
  fake_lookup_.device().set_connectivity_state(ConnectivityState::READY);
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadEnabled());
  // Set to attached, and confirm.
  fake_lookup_.device().set_connectivity_state(ConnectivityState::ATTACHED);
  EXPECT_TRUE(ThreadStackMgrImpl()._IsThreadEnabled());
}

TEST_F(ThreadStackManagerTest, SetEnabled) {
  // Sanity check starting state.
  ASSERT_EQ(fake_lookup_.device().connectivity_state(), ConnectivityState::INACTIVE);
  // Alternate enabling/disabling and confirming the current state.
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadEnabled(true), WEAVE_NO_ERROR);
  EXPECT_EQ(fake_lookup_.device().connectivity_state(), ConnectivityState::OFFLINE);
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadEnabled(false), WEAVE_NO_ERROR);
  EXPECT_EQ(fake_lookup_.device().connectivity_state(), ConnectivityState::INACTIVE);
}

TEST_F(ThreadStackManagerTest, IsAttached) {
  // Confirm initial INACTIVE => false.
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadAttached());
  // Set to attached and confirm.
  fake_lookup_.device().set_connectivity_state(ConnectivityState::ATTACHED);
  EXPECT_TRUE(ThreadStackMgrImpl()._IsThreadAttached());
}

TEST_F(ThreadStackManagerTest, GetProvisionNoCredential) {
  constexpr uint32_t kFakePANId = 12345;
  constexpr uint8_t kFakeChannel = 12;
  const std::string kFakeNetworkName = "fake-net-name";
  const std::vector<uint8_t> kFakeExtendedId{0, 1, 2, 3, 4, 5, 6, 7};
  const std::vector<uint8_t> kFakeMasterKey{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

  // Set up device info.
  fake_lookup_.device()
      .identity()
      .set_net_type(fuchsia::lowpan::NET_TYPE_THREAD_1_X)
      .set_raw_name(std::vector<uint8_t>(kFakeNetworkName.begin(), kFakeNetworkName.end()))
      .set_panid(kFakePANId)
      .set_channel(kFakeChannel)
      .set_xpanid(kFakeExtendedId);
  fake_lookup_.device().credential().set_master_key(kFakeMasterKey);

  // Get the provision.
  DeviceNetworkInfo net_info;
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadProvision(net_info, false), WEAVE_NO_ERROR);

  EXPECT_TRUE(net_info.FieldPresent.ThreadExtendedPANId);
  EXPECT_FALSE(net_info.FieldPresent.ThreadNetworkKey);

  EXPECT_EQ(kFakeNetworkName, std::string(net_info.ThreadNetworkName));
  EXPECT_TRUE(
      std::equal(kFakeExtendedId.begin(), kFakeExtendedId.end(), net_info.ThreadExtendedPANId))
      << "Expected " << FormatBytes(kFakeExtendedId) << "; recieved "
      << FormatBytes(std::vector<uint8_t>(
             net_info.ThreadExtendedPANId,
             net_info.ThreadExtendedPANId + DeviceNetworkInfo::kThreadExtendedPANIdLength));
  EXPECT_EQ(kFakeChannel, net_info.ThreadChannel);
  EXPECT_EQ(kFakePANId, net_info.ThreadPANId);
}

TEST_F(ThreadStackManagerTest, GetProvisionWithCredential) {
  constexpr uint32_t kFakePANId = 12345;
  constexpr uint8_t kFakeChannel = 12;
  const std::string kFakeNetworkName = "fake-net-name";
  const std::vector<uint8_t> kFakeExtendedId{0, 1, 2, 3, 4, 5, 6, 7};
  const std::vector<uint8_t> kFakeMasterKey{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

  // Set up device info.
  fake_lookup_.device()
      .identity()
      .set_net_type(fuchsia::lowpan::NET_TYPE_THREAD_1_X)
      .set_raw_name(std::vector<uint8_t>(kFakeNetworkName.begin(), kFakeNetworkName.end()))
      .set_panid(kFakePANId)
      .set_channel(kFakeChannel)
      .set_xpanid(kFakeExtendedId);
  fake_lookup_.device().credential().set_master_key(kFakeMasterKey);

  // Get the provision.
  DeviceNetworkInfo net_info;
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadProvision(net_info, true), WEAVE_NO_ERROR);

  EXPECT_TRUE(net_info.FieldPresent.ThreadExtendedPANId);
  EXPECT_TRUE(net_info.FieldPresent.ThreadNetworkKey);

  EXPECT_EQ(kFakeNetworkName, std::string(net_info.ThreadNetworkName));
  EXPECT_TRUE(
      std::equal(kFakeExtendedId.begin(), kFakeExtendedId.end(), net_info.ThreadExtendedPANId))
      << "Expected " << FormatBytes(kFakeExtendedId) << "; recieved "
      << FormatBytes(std::vector<uint8_t>(
             net_info.ThreadExtendedPANId,
             net_info.ThreadExtendedPANId + DeviceNetworkInfo::kThreadExtendedPANIdLength));
  EXPECT_TRUE(std::equal(kFakeMasterKey.begin(), kFakeMasterKey.end(), net_info.ThreadNetworkKey))
      << "Expected " << FormatBytes(kFakeExtendedId) << "; recieved "
      << FormatBytes(std::vector<uint8_t>(
             net_info.ThreadNetworkKey,
             net_info.ThreadNetworkKey + DeviceNetworkInfo::kThreadNetworkKeyLength));
  EXPECT_EQ(kFakeChannel, net_info.ThreadChannel);
  EXPECT_EQ(kFakePANId, net_info.ThreadPANId);
}

TEST_F(ThreadStackManagerTest, SetProvision) {
  constexpr uint32_t kFakePANId = 12345;
  constexpr uint8_t kFakeChannel = 12;
  const std::string kFakeNetworkName = "fake-net-name";
  const std::vector<uint8_t> kFakeExtendedId{0, 1, 2, 3, 4, 5, 6, 7};
  const std::vector<uint8_t> kFakeMasterKey{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

  // Set up provisioning info.
  DeviceNetworkInfo net_info;
  net_info.ThreadPANId = kFakePANId;
  net_info.ThreadChannel = kFakeChannel;
  std::memcpy(net_info.ThreadNetworkName, kFakeNetworkName.data(),
              std::min<size_t>(kFakeNetworkName.size() + 1,
                               DeviceNetworkInfo::kMaxThreadNetworkNameLength));
  std::memcpy(
      net_info.ThreadExtendedPANId, kFakeExtendedId.data(),
      std::min<size_t>(kFakeExtendedId.size(), DeviceNetworkInfo::kThreadExtendedPANIdLength));
  net_info.FieldPresent.ThreadExtendedPANId = true;
  std::memcpy(net_info.ThreadNetworkKey, kFakeMasterKey.data(),
              std::min<size_t>(kFakeMasterKey.size(), DeviceNetworkInfo::kThreadNetworkKeyLength));
  net_info.FieldPresent.ThreadNetworkKey = true;

  // Set provision, check pre- and post-conditions.
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadProvision(net_info), WEAVE_NO_ERROR);
  EXPECT_TRUE(ThreadStackMgrImpl()._IsThreadProvisioned());

  // Confirm identity.
  auto& identity = fake_lookup_.device().identity();
  EXPECT_EQ(identity.raw_name(),
            (std::vector<uint8_t>{kFakeNetworkName.data(),
                                  kFakeNetworkName.data() + kFakeNetworkName.size()}));
  EXPECT_EQ(identity.xpanid(), kFakeExtendedId);
  EXPECT_EQ(identity.panid(), kFakePANId);
  EXPECT_EQ(identity.channel(), kFakeChannel);

  // Confirm credential.
  auto& credential = fake_lookup_.device().credential();
  EXPECT_EQ(credential.master_key(), kFakeMasterKey);
}

TEST_F(ThreadStackManagerTest, ClearProvision) {
  constexpr uint32_t kFakePANId = 12345;
  constexpr uint8_t kFakeChannel = 12;
  const std::string kFakeNetworkName = "fake-net-name";
  const std::vector<uint8_t> kFakeExtendedId{0, 1, 2, 3, 4, 5, 6, 7};
  const std::vector<uint8_t> kFakeMasterKey{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  auto& device = fake_lookup_.device();

  // Set up device info.
  device.identity()
      .set_net_type(fuchsia::lowpan::NET_TYPE_THREAD_1_X)
      .set_raw_name(std::vector<uint8_t>(kFakeNetworkName.begin(), kFakeNetworkName.end()))
      .set_panid(kFakePANId)
      .set_channel(kFakeChannel)
      .set_xpanid(kFakeExtendedId);
  device.credential().set_master_key(kFakeMasterKey);
  device.set_connectivity_state(ConnectivityState::READY);

  // Clear provision, check pre- and post-conditions.
  EXPECT_TRUE(ThreadStackMgrImpl()._IsThreadProvisioned());
  ThreadStackMgrImpl()._ClearThreadProvision();
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
}

TEST_F(ThreadStackManagerTest, GetThreadDeviceType) {
  // Sanity check starting state.
  ASSERT_EQ(fake_lookup_.device().role(), Role::DETACHED);

  // Test various roles and associated device type.
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(), ThreadDeviceType::kThreadDeviceType_NotSupported);

  fake_lookup_.device().set_role(Role::LEADER);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(), ThreadDeviceType::kThreadDeviceType_Router);

  fake_lookup_.device().set_role(Role::END_DEVICE);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(), ThreadDeviceType::kThreadDeviceType_FullEndDevice);

  fake_lookup_.device().set_role(Role::SLEEPY_ROUTER);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(), ThreadDeviceType::kThreadDeviceType_Router);

  fake_lookup_.device().set_role(Role::SLEEPY_END_DEVICE);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(), ThreadDeviceType::kThreadDeviceType_SleepyEndDevice);

  fake_lookup_.device().set_role(Role::ROUTER);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(), ThreadDeviceType::kThreadDeviceType_Router);
}

TEST_F(ThreadStackManagerTest, ClearProvisionWithDeviceNotBound) {
  // Create a new delegate with an unbound device.
  ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
  // ClearThreadProvision should not crash when called with an unbound device.
  ThreadStackMgrImpl()._ClearThreadProvision();
}

}  // namespace testing
}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
