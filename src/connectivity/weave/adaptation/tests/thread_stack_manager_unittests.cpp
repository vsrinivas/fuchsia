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
#include <Weave/DeviceLayer/ThreadStackManager.h>
// clang-format on

#include "weave_test_fixture.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {
namespace testing {

namespace {
using fuchsia::lowpan::ConnectivityState;
using fuchsia::lowpan::Role;
using fuchsia::lowpan::device::Device;
using fuchsia::lowpan::device::DeviceState;
using fuchsia::lowpan::device::Lookup;
using fuchsia::lowpan::device::Lookup_LookupDevice_Response;
using fuchsia::lowpan::device::Lookup_LookupDevice_Result;
using fuchsia::lowpan::device::Protocols;
using fuchsia::lowpan::device::ServiceError;

const char kFakeInterfaceName[] = "fake0";
}  // namespace

class FakeLowpanDevice final : public fuchsia::lowpan::device::testing::Device_TestBase,
                               public fuchsia::lowpan::device::testing::DeviceExtra_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  // Fidl interfaces.

  void GetSupportedNetworkTypes(GetSupportedNetworkTypesCallback callback) override {
    callback({fuchsia::lowpan::NET_TYPE_THREAD_1_X});
  }

  void WatchDeviceState(WatchDeviceStateCallback callback) override {
    callback(std::move(DeviceState().set_role(role_).set_connectivity_state(connectivity_state_)));
  }

  void SetActive(bool active, SetActiveCallback callback) override {
    if (active) {
      connectivity_state_ = ConnectivityState::OFFLINE;
    } else {
      connectivity_state_ = ConnectivityState::INACTIVE;
    }
    callback();
  }

  // Accessors/mutators for testing.

  void set_dispatcher(async_dispatcher_t* dispatcher) { dispatcher_ = dispatcher; }

  ConnectivityState connectivity_state() { return connectivity_state_; }

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

}  // namespace testing
}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
