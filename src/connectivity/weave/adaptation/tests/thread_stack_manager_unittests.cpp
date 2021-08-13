// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/lowpan/device/cpp/fidl_test_base.h>
#include <fuchsia/lowpan/thread/cpp/fidl_test_base.h>
#include <fuchsia/net/routes/cpp/fidl.h>
#include <fuchsia/net/routes/cpp/fidl_test_base.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <gtest/gtest.h>

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
// clang-format on

#include "src/connectivity/weave/adaptation/thread_stack_manager_delegate_impl.h"
#include "test_configuration_manager.h"
#include "weave_test_fixture.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {
namespace testing {

namespace {

using weave::adaptation::testing::TestConfigurationManager;

using fuchsia::lowpan::ConnectivityState;
using fuchsia::lowpan::Credential;
using fuchsia::lowpan::Identity;
using fuchsia::lowpan::JoinParams;
using fuchsia::lowpan::ProvisioningParams;
using fuchsia::lowpan::Role;
using fuchsia::lowpan::device::AllCounters;
using fuchsia::lowpan::device::Counters;
using fuchsia::lowpan::device::Device;
using fuchsia::lowpan::device::DeviceExtra;
using fuchsia::lowpan::device::DeviceState;
using fuchsia::lowpan::device::Lookup;
using fuchsia::lowpan::device::Lookup_LookupDevice_Response;
using fuchsia::lowpan::device::Lookup_LookupDevice_Result;
using fuchsia::lowpan::device::MacCounters;
using fuchsia::lowpan::device::Protocols;
using fuchsia::lowpan::device::ProvisionError;
using fuchsia::lowpan::device::ProvisioningMonitor;
using fuchsia::lowpan::device::ProvisioningMonitor_WatchProgress_Response;
using fuchsia::lowpan::device::ProvisioningMonitor_WatchProgress_Result;
using fuchsia::lowpan::device::ProvisioningProgress;
using fuchsia::lowpan::device::ServiceError;
using fuchsia::lowpan::thread::LegacyJoining;
using fuchsia::net::IpAddress;
using fuchsia::net::Ipv4Address;
using fuchsia::net::Ipv6Address;
using fuchsia::net::routes::Destination;
using fuchsia::net::routes::Resolved;
using fuchsia::net::routes::State_Resolve_Response;
using fuchsia::net::routes::State_Resolve_Result;

using ThreadDeviceType = ConnectivityManager::ThreadDeviceType;
using nl::Inet::IPAddress;
using nl::Weave::DeviceLayer::ThreadStackManagerDelegateImpl;
using nl::Weave::Profiles::NetworkProvisioning::kNetworkType_Thread;

using Schema::Nest::Trait::Network::TelemetryNetworkWpanTrait::NetworkWpanStatsEvent;

namespace routes = fuchsia::net::routes;

// Note on thread synchronization --
//
// Due to the fact that the Weave DeviceLayer APIs are inherently synchronous,
// FIDL calls are made synchronously. This means that simply having a loop run
// to process FIDL calls that can be completely controlled by the test is not
// possible, so the dispatcher that handles FIDL calls runs in a background
// thread in these tests (see WeaveTestFixture for details on the background
// thread).
//
// In addition, high-level C++ FIDL bindings must be run on the same thread,
// meaning it is not possible to save a FIDL callback on one thread, destroy
// the thread, pump the async dispatcher manually, and then kick it back to
// another thread, since the threads will not match.
//
// For that reason, there are tests below which require synchronization with
// the background thread to properly check behavior of ThreadStackManager.
// The relevant state changes are synchronized with mutexes and condition
// variables to ensure the test can check the correct values.
//
// There are plans for the adaptation to  use async FIDL calls in delegates,
// and synchronizing at the API boundary to allow tests like these to bypass
// these issues. At that point, these synchronization steps can be removed.

const char kFakeInterfaceName[] = "fake0";

constexpr char kTestV4AddrStr[] = "1.2.3.4";
constexpr char kTestV6AddrStr[] = "0102:0304:0506:0708:090A:0B0C:0D0E:0F00";
constexpr char kTestV4AddrBad[] = "4.3.2.1";
constexpr char kTestV6AddrBad[] = "0A0B:0C0D:0E0F:0001:0203:0405:0607:0809";
constexpr char kTestV4AddrVal[] = {1, 2, 3, 4};
constexpr char kTestV6AddrVal[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0};

constexpr char kFakePairingCode[] = "F4K3C0D3";
constexpr char kFakeVendor[] = "Fuchsia Vendor";
constexpr char kFakeProduct[] = "Thread-enabled Product";
constexpr char kFakeVersion[] = "12.34.567";
constexpr uint32_t kJoiningTimeMaxMsec = 120000;
constexpr uint32_t kJoiningRetryDelayMsec = 10000;

// The maximum amount of time to wait for threads to synchronize in tests. See
// note on thread syncrhonization above for details. This allows a test to
// fail rather than hang if the state does not update.
//
// FLAKE WARNING: If tests that use the wait functions are flaking, increase
// this value. However the current value should be way, way more than enough to
// reliably synchronize.
constexpr std::chrono::milliseconds kMaxSyncTime{10};

// The required size of a buffer supplied to GetPrimary802154MACAddress.
constexpr size_t k802154MacAddressBufSize =
    sizeof(Profiles::DeviceDescription::WeaveDeviceDescriptor::Primary802154MACAddress);

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
  class FakeProvisioningMonitor final
      : public fuchsia::lowpan::device::testing::ProvisioningMonitor_TestBase {
   public:
    FakeProvisioningMonitor(FakeLowpanDevice* owner) : owner_(owner) {}
    void NotImplemented_(const std::string& name) override {
      FAIL() << "Not implemented: " << name;
    }

    void set_params(JoinParams params) { params_ = std::move(params); }
    const JoinParams& params() const { return params_; }

    bool IsWatched() { return callback_.operator bool(); }
    void WatchProgress(WatchProgressCallback callback) override {
      std::lock_guard<std::mutex> lock{watched_sync_};
      callback_ = std::move(callback);
    }

    void CompleteError(ProvisionError error) {
      std::lock_guard<std::mutex> lock{watched_sync_};
      WatchProgressCallback callback = std::move(callback_);
      watched_signal_.notify_one();
      ProvisioningMonitor_WatchProgress_Result result;
      result.set_err(error);
      async::PostTask(owner_->dispatcher_,
                      [callback = std::move(callback), result = std::move(result)]() mutable {
                        callback(std::move(result));
                      });
    }

    void CompleteOk(Identity identity) {
      std::lock_guard<std::mutex> lock{watched_sync_};
      WatchProgressCallback callback = std::move(callback_);
      watched_signal_.notify_one();
      ProvisioningMonitor_WatchProgress_Result result;

      Identity cloned_identity;
      identity.Clone(&cloned_identity);
      result.set_response(ProvisioningMonitor_WatchProgress_Response(
          ProvisioningProgress::WithIdentity(std::move(cloned_identity))));
      owner_->identity_ = std::move(identity);

      // Transition state.
      switch (owner_->connectivity_state_) {
        case ConnectivityState::INACTIVE:
          owner_->connectivity_state_ = ConnectivityState::READY;
          break;
        case ConnectivityState::OFFLINE:
        case ConnectivityState::COMMISSIONING:
          owner_->connectivity_state_ = ConnectivityState::ATTACHED;
          break;
        default:
          // Do nothing, device is already in the provisioned state.
          break;
      }

      async::PostTask(owner_->dispatcher_,
                      [callback = std::move(callback), result = std::move(result)]() mutable {
                        callback(std::move(result));
                      });
    }

    void UpdateProgress(double progress) {
      std::lock_guard<std::mutex> lock{watched_sync_};
      WatchProgressCallback callback = std::move(callback_);
      watched_signal_.notify_one();
      if (progress < 0)
        progress = 0;
      if (progress > 1)
        progress = 1;

      async::PostTask(owner_->dispatcher_, [=, callback = std::move(callback)]() mutable {
        callback(ProvisioningMonitor_WatchProgress_Result::WithResponse(
            ProvisioningMonitor_WatchProgress_Response(
                ProvisioningProgress::WithProgress(progress))));
      });
    }

    void Reset() {
      params_ = JoinParams();
      callback_ = WatchProgressCallback();
    }

    bool WaitForWatch(bool watched) {
      std::unique_lock<std::mutex> lock{watched_sync_};

      if (IsWatched() != watched) {
        watched_signal_.wait_for(lock, kMaxSyncTime, [&] { return IsWatched() == watched; });
      }

      // Return bind state retrieved inside mutex lock.
      return IsWatched();
    }

   private:
    FakeLowpanDevice* owner_;
    JoinParams params_;
    WatchProgressCallback callback_;
    std::mutex watched_sync_;
    std::condition_variable watched_signal_;
  };

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

  void JoinNetwork(JoinParams params,
                   fidl::InterfaceRequest<ProvisioningMonitor> request) override {
    std::lock_guard<std::mutex> lock{prov_mon_bind_sync_};
    provisioning_monitor_.set_params(std::move(params));
    provisioning_monitor_binding_.Bind(std::move(request), dispatcher_);
    prov_mon_bind_signal_.notify_one();
    provisioning_monitor_binding_.set_error_handler([this](zx_status_t) {
      prov_mon_bind_signal_.notify_one();
      provisioning_monitor_.Reset();
    });
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

  // Utilities for testing.

  bool WaitForProvisioningMonitorBinding(bool bound) {
    std::unique_lock<std::mutex> lock{prov_mon_bind_sync_};

    if (is_provisioning_monitor_bound() != bound) {
      prov_mon_bind_signal_.wait_for(lock, kMaxSyncTime,
                                     [&] { return is_provisioning_monitor_bound() == bound; });
    }

    // Return bind state retrieved inside mutex lock.
    return is_provisioning_monitor_bound();
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

  FakeProvisioningMonitor& provisioning_monitor() { return provisioning_monitor_; }
  bool is_provisioning_monitor_bound() { return provisioning_monitor_binding_.is_bound(); }

 private:
  WatchDeviceStateCallback watch_device_state_callback_;
  async_dispatcher_t* dispatcher_;

  ConnectivityState connectivity_state_{ConnectivityState::INACTIVE};
  Credential credential_;
  Identity identity_;
  Role role_{Role::DETACHED};

  FakeProvisioningMonitor provisioning_monitor_{this};
  fidl::Binding<ProvisioningMonitor> provisioning_monitor_binding_{&provisioning_monitor_};
  std::mutex prov_mon_bind_sync_;
  std::condition_variable prov_mon_bind_signal_;
};

class FakeThreadLegacy : public fuchsia::lowpan::thread::testing::LegacyJoining_TestBase {
 public:
  using CallList = std::vector<std::pair<zx_duration_t, uint16_t>>;

  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  void MakeJoinable(zx_duration_t duration, uint16_t port, MakeJoinableCallback callback) override {
    calls_.emplace_back(duration, port);

    if (return_status_ != ZX_OK) {
      bindings_->CloseBinding(this, return_status_);
    } else {
      callback();
    }
  }

  void SetReturnStatus(zx_status_t return_status) { return_status_ = return_status; }

  const CallList& calls() { return calls_; }

  void SetBindingSet(fidl::BindingSet<LegacyJoining>* bindings) { bindings_ = bindings; }

 private:
  CallList calls_;
  zx_status_t return_status_ = ZX_OK;
  fidl::BindingSet<LegacyJoining>* bindings_ = nullptr;
};

class FakeCounters : public fuchsia::lowpan::device::testing::Counters_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  void Get(GetCallback callback) override {
    if (return_status_ != ZX_OK) {
      bindings_->CloseBinding(this, return_status_);
    } else {
      callback(std::move(counters_));
    }
  }

  void SetReturnStatus(zx_status_t return_status) { return_status_ = return_status; }

  void SetCounters(AllCounters counters) { counters_ = std::move(counters); }

  void SetBindingSet(fidl::BindingSet<Counters>* bindings) { bindings_ = bindings; }

 private:
  zx_status_t return_status_ = ZX_OK;
  AllCounters counters_;
  fidl::BindingSet<Counters>* bindings_ = nullptr;
};

class FakeLowpanLookup final : public fuchsia::lowpan::device::testing::Lookup_TestBase {
 public:
  FakeLowpanLookup() {
    thread_legacy_.SetBindingSet(&thread_legacy_bindings_);
    counters_.SetBindingSet(&counters_bindings_);
  }

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

    if (protocols.has_thread_legacy_joining()) {
      thread_legacy_bindings_.AddBinding(
          &thread_legacy_, std::move(*protocols.mutable_thread_legacy_joining()), dispatcher_);
    }

    if (protocols.has_counters()) {
      counters_bindings_.AddBinding(&counters_, std::move(*protocols.mutable_counters()),
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
  FakeThreadLegacy& thread_legacy() { return thread_legacy_; }
  FakeCounters& counters() { return counters_; }

 private:
  FakeLowpanDevice device_;
  FakeThreadLegacy thread_legacy_;
  FakeCounters counters_;
  fidl::BindingSet<Device> device_bindings_;
  fidl::BindingSet<DeviceExtra> device_extra_bindings_;
  fidl::BindingSet<LegacyJoining> thread_legacy_bindings_;
  fidl::BindingSet<Counters> counters_bindings_;
  async_dispatcher_t* dispatcher_;
  fidl::Binding<Lookup> binding_{this};
};

class FakeNetRoutes : public fuchsia::net::routes::testing::State_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  void Resolve(IpAddress dest, ResolveCallback callback) override {
    State_Resolve_Result result;
    if (dest.is_ipv4()) {
      static_assert(sizeof(dest.ipv4().addr) == sizeof(kTestV4AddrVal));
      if (std::memcmp(dest.ipv4().addr.data(), kTestV4AddrVal, sizeof(kTestV4AddrVal)) == 0) {
        State_Resolve_Response response(
            Resolved::WithDirect(std::move(Destination().set_address(std::move(dest)))));
        result.set_response(std::move(response));
      } else {
        result.set_err(ZX_ERR_ADDRESS_UNREACHABLE);
      }
    } else {
      static_assert(sizeof(dest.ipv6().addr) == sizeof(kTestV6AddrVal));
      if (std::memcmp(dest.ipv6().addr.data(), kTestV6AddrVal, sizeof(kTestV6AddrVal)) == 0) {
        State_Resolve_Response response(
            Resolved::WithDirect(std::move(Destination().set_address(std::move(dest)))));
        result.set_response(std::move(response));
      } else {
        result.set_err(ZX_ERR_ADDRESS_UNREACHABLE);
      }
    }
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<routes::State> GetHandler(async_dispatcher_t* dispatcher) {
    return [this, dispatcher](fidl::InterfaceRequest<routes::State> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

 private:
  fidl::Binding<routes::State> binding_{this};
};

class MockedEventLoggingThreadStackManagerDelegateImpl : public ThreadStackManagerDelegateImpl {
 public:
  static constexpr nl::Weave::Profiles::DataManagement::event_id_t kFakeEventId = 42;

  const NetworkWpanStatsEvent& network_wpan_stats_event() { return network_wpan_stats_event_; }

  size_t CountLogNetworkWpanStatsEvent() { return count_log_network_wpan_stats_event_; }

 private:
  nl::Weave::Profiles::DataManagement::event_id_t LogNetworkWpanStatsEvent(
      NetworkWpanStatsEvent* event) override {
    ++count_log_network_wpan_stats_event_;
    network_wpan_stats_event_ = *event;
    return kFakeEventId;
  }

  WEAVE_ERROR StartJoiningTimeout(uint32_t, fit::closure) override { return WEAVE_NO_ERROR; }
  WEAVE_ERROR StartJoiningRetry(uint32_t, fit::closure) override { return WEAVE_NO_ERROR; }

  NetworkWpanStatsEvent network_wpan_stats_event_ = {};
  size_t count_log_network_wpan_stats_event_ = 0;
};

// Mocked time for testing with timers.
class MockClock {
 public:
  struct MockTimer {
    uint32_t start_time;
    uint32_t delay;
    std::shared_ptr<fit::closure> callback;

    uint32_t deadline() const { return start_time + delay; }
  };

  // Returns "now" in fake test time. 0 is the beginning of the test.
  uint32_t Now() const { return now_; }

  // Add a timer.
  void AddTimer(uint32_t delay, fit::closure callback) {
    // Prevent infinite loop, no 0-delay timer allowed.
    if (delay == 0) {
      return;
    }
    queue_.push({now_, delay, std::make_shared<fit::closure>(std::move(callback))});
  }

  // Advance timers.
  void AdvanceBy(uint32_t duration) {
    uint32_t deadline = Now() + duration;
    while (!queue_.empty() && queue_.top().deadline() <= deadline) {
      MockTimer next = queue_.top();
      queue_.pop();
      now_ = next.deadline();
      (*next.callback)();
    }
    now_ = deadline;
  }

  // The number of active timers.
  size_t TimerCount() const { return queue_.size(); }

 private:
  uint32_t now_{0u};
  const static auto constexpr compare_timer_ = [](const MockTimer& l, const MockTimer& r) {
    return l.deadline() > r.deadline();
  };
  std::priority_queue<MockTimer, std::vector<MockTimer>, decltype(compare_timer_)> queue_{
      compare_timer_};
};

class MockedTimerThreadStackManagerDelegateImpl
    : public MockedEventLoggingThreadStackManagerDelegateImpl {
 public:
  MockClock& clock() { return clock_; }

  bool timeout_active() { return timeout_active_; }
  bool retry_active() { return retry_active_; }

 private:
  MockClock clock_;
  bool timeout_active_{false};
  bool retry_active_{false};

  WEAVE_ERROR StartJoiningTimeout(uint32_t delay, fit::closure callback) override {
    timeout_active_ = true;
    clock_.AddTimer(delay, std::move(callback));
    return WEAVE_NO_ERROR;
  }

  WEAVE_ERROR StartJoiningRetry(uint32_t delay, fit::closure callback) override {
    retry_active_ = true;
    clock_.AddTimer(delay, std::move(callback));
    return WEAVE_NO_ERROR;
  }

  void CancelJoiningTimeout() override { timeout_active_ = false; }

  void CancelJoiningRetry() override { retry_active_ = false; }
};

class ThreadStackManagerTest : public WeaveTestFixture<> {
 public:
  ThreadStackManagerTest() {
    context_provider_.service_directory_provider()->AddService(
        fake_lookup_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_routes_.GetHandler(dispatcher()));
  }

  void ResetConfigMgr() {
    ConfigurationMgrImpl().SetDelegate(nullptr);
    ConfigurationMgrImpl().SetDelegate(std::make_unique<TestConfigurationManager>());
  }

  void SetUp() override {
    WeaveTestFixture<>::SetUp();
    PlatformMgrImpl().SetComponentContextForProcess(context_provider_.TakeContext());
    RunFixtureLoop();
    ResetConfigMgr();
    ConfigurationMgr().StorePairingCode(kFakePairingCode, sizeof(kFakePairingCode));
    configuration_delegate().set_is_thread_enabled(true);
    configuration_delegate().set_vendor_id_description(std::string(kFakeVendor));
    configuration_delegate().set_product_id_description(std::string(kFakeProduct));
    configuration_delegate().set_firmware_revision(std::string(kFakeVersion));
    auto tsm_delegate = std::make_unique<MockedEventLoggingThreadStackManagerDelegateImpl>();
    tsm_delegate_ = tsm_delegate.get();
    ThreadStackMgrImpl().SetDelegate(std::move(tsm_delegate));
    ASSERT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_NO_ERROR);
  }

  void TearDown() override {
    StopFixtureLoop();
    WeaveTestFixture<>::TearDown();
    ThreadStackMgrImpl().SetDelegate(nullptr);
    ConfigurationMgrImpl().SetDelegate(nullptr);
  }

 protected:
  TestConfigurationManager& configuration_delegate() {
    return *reinterpret_cast<TestConfigurationManager*>(ConfigurationMgrImpl().GetDelegate());
  }

  FakeLowpanLookup fake_lookup_;
  FakeNetRoutes fake_routes_;
  MockedEventLoggingThreadStackManagerDelegateImpl* tsm_delegate_;

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
  DeviceNetworkInfo net_info;

  // Expect failure getting Thread provision.
  EXPECT_NE(ThreadStackMgrImpl()._GetThreadProvision(net_info, false), WEAVE_NO_ERROR);

  // Set up device info.
  fake_lookup_.device()
      .identity()
      .set_net_type(fuchsia::lowpan::NET_TYPE_THREAD_1_X)
      .set_raw_name(std::vector<uint8_t>(kFakeNetworkName.begin(), kFakeNetworkName.end()))
      .set_panid(kFakePANId)
      .set_channel(kFakeChannel)
      .set_xpanid(kFakeExtendedId);
  fake_lookup_.device().credential().set_master_key(kFakeMasterKey);
  fake_lookup_.device().set_connectivity_state(ConnectivityState::READY);

  // Get the provision.
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadProvision(net_info, false), WEAVE_NO_ERROR);

  EXPECT_EQ(net_info.NetworkType, kNetworkType_Thread);
  EXPECT_TRUE(net_info.FieldPresent.NetworkId);
  EXPECT_EQ(net_info.NetworkId, Internal::kThreadNetworkId);
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
  DeviceNetworkInfo net_info;

  // Expect failure getting Thread provision.
  EXPECT_NE(ThreadStackMgrImpl()._GetThreadProvision(net_info, false), WEAVE_NO_ERROR);

  // Set up device info.
  fake_lookup_.device()
      .identity()
      .set_net_type(fuchsia::lowpan::NET_TYPE_THREAD_1_X)
      .set_raw_name(std::vector<uint8_t>(kFakeNetworkName.begin(), kFakeNetworkName.end()))
      .set_panid(kFakePANId)
      .set_channel(kFakeChannel)
      .set_xpanid(kFakeExtendedId);
  fake_lookup_.device().credential().set_master_key(kFakeMasterKey);
  fake_lookup_.device().set_connectivity_state(ConnectivityState::READY);

  // Get the provision.
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadProvision(net_info, true), WEAVE_NO_ERROR);

  EXPECT_EQ(net_info.NetworkType, kNetworkType_Thread);
  EXPECT_TRUE(net_info.FieldPresent.NetworkId);
  EXPECT_EQ(net_info.NetworkId, Internal::kThreadNetworkId);
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

TEST_F(ThreadStackManagerTest, SetProvisionMissingData) {
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

  // Set provision with missing items.
  net_info.FieldPresent.ThreadExtendedPANId = false;
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadProvision(net_info), WEAVE_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  net_info.FieldPresent.ThreadExtendedPANId = true;

  net_info.ThreadChannel = Profiles::NetworkProvisioning::kThreadChannel_NotSpecified;
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadProvision(net_info), WEAVE_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  net_info.ThreadChannel = kFakeChannel;

  net_info.ThreadPANId = Profiles::NetworkProvisioning::kThreadPANId_NotSpecified;
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadProvision(net_info), WEAVE_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  net_info.ThreadPANId = kFakePANId;

  net_info.FieldPresent.ThreadNetworkKey = false;
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadProvision(net_info), WEAVE_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  net_info.FieldPresent.ThreadNetworkKey = true;

  // Confirm identity has not been set.
  auto& identity = fake_lookup_.device().identity();
  EXPECT_FALSE(identity.has_raw_name());
  EXPECT_FALSE(identity.has_xpanid());
  EXPECT_FALSE(identity.has_panid());
  EXPECT_FALSE(identity.has_channel());

  // Confirm credential has not been set.
  auto& credential = fake_lookup_.device().credential();
  EXPECT_TRUE(credential.has_invalid_tag());
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
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(),
            ThreadDeviceType::kThreadDeviceType_NotSupported);

  fake_lookup_.device().set_role(Role::LEADER);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(),
            ThreadDeviceType::kThreadDeviceType_Router);

  fake_lookup_.device().set_role(Role::END_DEVICE);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(),
            ThreadDeviceType::kThreadDeviceType_FullEndDevice);

  fake_lookup_.device().set_role(Role::SLEEPY_ROUTER);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(),
            ThreadDeviceType::kThreadDeviceType_Router);

  fake_lookup_.device().set_role(Role::SLEEPY_END_DEVICE);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(),
            ThreadDeviceType::kThreadDeviceType_SleepyEndDevice);

  fake_lookup_.device().set_role(Role::ROUTER);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadDeviceType(),
            ThreadDeviceType::kThreadDeviceType_Router);
}

TEST_F(ThreadStackManagerTest, ClearProvisionWithDeviceNotBound) {
  // Create a new delegate with an unbound device.
  ThreadStackMgrImpl().SetDelegate(nullptr);
  ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
  // ClearThreadProvision should not crash when called with an unbound device.
  ThreadStackMgrImpl()._ClearThreadProvision();
}

TEST_F(ThreadStackManagerTest, ThreadSupportDisabled) {
  // Reset TSM to uninitialized state.
  ThreadStackMgrImpl().SetDelegate(nullptr);
  ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());

  // Initialize TSM with Thread disabled in the config mgr.
  configuration_delegate().set_is_thread_enabled(false);
  ASSERT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_NO_ERROR);

  EXPECT_FALSE(ThreadStackMgrImpl().IsThreadSupported());
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadEnabled());
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadProvisioned());
  EXPECT_FALSE(ThreadStackMgrImpl()._IsThreadAttached());
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadEnabled(false), WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE);
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadEnabled(true), WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE);

  uint8_t buf[k802154MacAddressBufSize] = {};
  EXPECT_EQ(ThreadStackMgr().GetPrimary802154MACAddress(buf),
            WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE);

  // Set up provisioning info, confirm Get/SetThreadProvision unsupported.
  constexpr uint32_t kFakePANId = 12345;
  constexpr uint8_t kFakeChannel = 12;
  const std::string kFakeNetworkName = "fake-net-name";
  const std::vector<uint8_t> kFakeExtendedId{0, 1, 2, 3, 4, 5, 6, 7};
  const std::vector<uint8_t> kFakeMasterKey{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

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
  EXPECT_EQ(ThreadStackMgrImpl()._SetThreadProvision(net_info),
            WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE);
  EXPECT_EQ(ThreadStackMgrImpl()._GetThreadProvision(net_info, false),
            WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE);
}

TEST_F(ThreadStackManagerTest, HaveRouteToAddress) {
  IPAddress addr;

  ASSERT_TRUE(IPAddress::FromString(kTestV4AddrStr, addr));
  EXPECT_TRUE(ThreadStackMgr().HaveRouteToAddress(addr));
  ASSERT_TRUE(IPAddress::FromString(kTestV4AddrBad, addr));
  EXPECT_FALSE(ThreadStackMgr().HaveRouteToAddress(addr));
  ASSERT_TRUE(IPAddress::FromString(kTestV6AddrStr, addr));
  EXPECT_TRUE(ThreadStackMgr().HaveRouteToAddress(addr));
  ASSERT_TRUE(IPAddress::FromString(kTestV6AddrBad, addr));
  EXPECT_FALSE(ThreadStackMgr().HaveRouteToAddress(addr));
}

TEST_F(ThreadStackManagerTest, GetPrimary802154MacAddress) {
  constexpr uint8_t expected[k802154MacAddressBufSize] = {0xFF};
  uint8_t mac_addr[k802154MacAddressBufSize];

  EXPECT_EQ(ThreadStackMgr().GetPrimary802154MACAddress(mac_addr), WEAVE_NO_ERROR);
  EXPECT_EQ(0, std::memcmp(expected, mac_addr, k802154MacAddressBufSize));
}

TEST_F(ThreadStackManagerTest, SetThreadJoinable) {
  constexpr uint32_t kTestDuration = 123;
  EXPECT_EQ(fake_lookup_.thread_legacy().calls().size(), 0u);

  EXPECT_EQ(ThreadStackMgrImpl().SetThreadJoinable(true), WEAVE_NO_ERROR);
  {
    const auto& calls = fake_lookup_.thread_legacy().calls();
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_NE(calls[0].first, 0);
    EXPECT_EQ(calls[0].second, WEAVE_UNSECURED_PORT);
  }

  configuration_delegate().set_thread_joinable_duration(kTestDuration);
  EXPECT_EQ(ThreadStackMgrImpl().SetThreadJoinable(true), WEAVE_NO_ERROR);
  {
    const auto& calls = fake_lookup_.thread_legacy().calls();
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[1].first, zx_duration_from_sec(kTestDuration));
    EXPECT_EQ(calls[1].second, WEAVE_UNSECURED_PORT);
  }

  EXPECT_EQ(ThreadStackMgrImpl().SetThreadJoinable(false), WEAVE_NO_ERROR);
  {
    const auto& calls = fake_lookup_.thread_legacy().calls();
    ASSERT_EQ(calls.size(), 3u);
    EXPECT_EQ(calls[2].first, 0);
    EXPECT_EQ(calls[2].second, WEAVE_UNSECURED_PORT);
  }
}

TEST_F(ThreadStackManagerTest, SetThreadJoinableFail) {
  EXPECT_EQ(fake_lookup_.thread_legacy().calls().size(), 0u);
  fake_lookup_.thread_legacy().SetReturnStatus(ZX_ERR_BAD_STATE);

  EXPECT_NE(ThreadStackMgrImpl().SetThreadJoinable(true), WEAVE_NO_ERROR);
  {
    const auto& calls = fake_lookup_.thread_legacy().calls();
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_NE(calls[0].first, 0);
    EXPECT_EQ(calls[0].second, WEAVE_UNSECURED_PORT);
  }

  EXPECT_NE(ThreadStackMgrImpl().SetThreadJoinable(false), WEAVE_NO_ERROR);
  {
    const auto& calls = fake_lookup_.thread_legacy().calls();
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[1].first, 0);
    EXPECT_EQ(calls[1].second, WEAVE_UNSECURED_PORT);
  }
}

TEST_F(ThreadStackManagerTest, GetAndLogThreadStatsCounters) {
  constexpr int32_t kTxFakeTotal = 1;
  constexpr int32_t kTxFakeUnicast = 2;
  constexpr int32_t kTxFakeBroadcast = 3;
  constexpr int32_t kTxFakeAckRequested = 4;
  constexpr int32_t kTxFakeAcked = 5;
  constexpr int32_t kTxFakeNoAckRequested = 6;
  constexpr int32_t kTxFakeData = 7;
  constexpr int32_t kTxFakeDataPoll = 8;
  constexpr int32_t kTxFakeBeacon = 9;
  constexpr int32_t kTxFakeBeaconRequest = 10;
  constexpr int32_t kTxFakeOther = 11;
  constexpr int32_t kTxFakeAddressFiltered = 12;
  constexpr int32_t kTxFakeRetries = 13;
  constexpr int32_t kTxFakeDirectMaxRetryExpiry = 14;
  constexpr int32_t kTxFakeIndirectMaxRetryExpiry = 15;
  constexpr int32_t kTxFakeErrCca = 23;
  constexpr int32_t kTxFakeErrAbort = 24;
  constexpr int32_t kTxFakeErrBusyChannel = 25;
  constexpr int32_t kTxFakeErrOther = 26;

  constexpr int32_t kRxFakeTotal = 27;
  constexpr int32_t kRxFakeUnicast = 28;
  constexpr int32_t kRxFakeBroadcast = 29;
  constexpr int32_t kRxFakeAckRequested = 30;
  constexpr int32_t kRxFakeAcked = 31;
  constexpr int32_t kRxFakeNoAckRequested = 32;
  constexpr int32_t kRxFakeData = 33;
  constexpr int32_t kRxFakeDataPoll = 34;
  constexpr int32_t kRxFakeBeacon = 35;
  constexpr int32_t kRxFakeBeaconRequest = 36;
  constexpr int32_t kRxFakeOther = 37;
  constexpr int32_t kRxFakeAddressFiltered = 38;
  constexpr int32_t kRxFakeDestAddrFiltered = 42;
  constexpr int32_t kRxFakeDuplicated = 43;
  constexpr int32_t kRxFakeErrNoFrame = 44;
  constexpr int32_t kRxFakeErrUnknownNeighbor = 45;
  constexpr int32_t kRxFakeErrInvalidSrcAddr = 46;
  constexpr int32_t kRxFakeErrSec = 47;
  constexpr int32_t kRxFakeErrFcs = 48;
  constexpr int32_t kRxFakeErrOther = 52;

  AllCounters counters;
  MacCounters tx;
  MacCounters rx;

  tx.set_total(kTxFakeTotal)
      .set_unicast(kTxFakeUnicast)
      .set_broadcast(kTxFakeBroadcast)
      .set_ack_requested(kTxFakeAckRequested)
      .set_acked(kTxFakeAcked)
      .set_no_ack_requested(kTxFakeNoAckRequested)
      .set_data(kTxFakeData)
      .set_data_poll(kTxFakeDataPoll)
      .set_beacon(kTxFakeBeacon)
      .set_beacon_request(kTxFakeBeaconRequest)
      .set_other(kTxFakeOther)
      .set_address_filtered(kTxFakeAddressFiltered)
      .set_retries(kTxFakeRetries)
      .set_direct_max_retry_expiry(kTxFakeDirectMaxRetryExpiry)
      .set_indirect_max_retry_expiry(kTxFakeIndirectMaxRetryExpiry)
      .set_err_cca(kTxFakeErrCca)
      .set_err_abort(kTxFakeErrAbort)
      .set_err_busy_channel(kTxFakeErrBusyChannel)
      .set_err_other(kTxFakeErrOther);

  rx.set_total(kRxFakeTotal)
      .set_unicast(kRxFakeUnicast)
      .set_broadcast(kRxFakeBroadcast)
      .set_ack_requested(kRxFakeAckRequested)
      .set_acked(kRxFakeAcked)
      .set_no_ack_requested(kRxFakeNoAckRequested)
      .set_data(kRxFakeData)
      .set_data_poll(kRxFakeDataPoll)
      .set_beacon(kRxFakeBeacon)
      .set_beacon_request(kRxFakeBeaconRequest)
      .set_other(kRxFakeOther)
      .set_address_filtered(kRxFakeAddressFiltered)
      .set_dest_addr_filtered(kRxFakeDestAddrFiltered)
      .set_duplicated(kRxFakeDuplicated)
      .set_err_no_frame(kRxFakeErrNoFrame)
      .set_err_unknown_neighbor(kRxFakeErrUnknownNeighbor)
      .set_err_invalid_src_addr(kRxFakeErrInvalidSrcAddr)
      .set_err_sec(kRxFakeErrSec)
      .set_err_fcs(kRxFakeErrFcs)
      .set_err_other(kRxFakeErrOther);

  counters.set_mac_tx(std::move(tx)).set_mac_rx(std::move(rx));
  fake_lookup_.counters().SetCounters(std::move(counters));

  EXPECT_EQ(ZX_OK, ThreadStackMgr().GetAndLogThreadStatsCounters());
  EXPECT_EQ(1u, tsm_delegate_->CountLogNetworkWpanStatsEvent());

  const NetworkWpanStatsEvent& event = tsm_delegate_->network_wpan_stats_event();

  EXPECT_EQ(event.phyTx, kTxFakeTotal);
  EXPECT_EQ(event.macUnicastTx, kTxFakeUnicast);
  EXPECT_EQ(event.macBroadcastTx, kTxFakeBroadcast);
  EXPECT_EQ(event.macTxAckReq, kTxFakeAckRequested);
  EXPECT_EQ(event.macTxAcked, kTxFakeAcked);
  EXPECT_EQ(event.macTxNoAckReq, kTxFakeNoAckRequested);
  EXPECT_EQ(event.macTxData, kTxFakeData);
  EXPECT_EQ(event.macTxDataPoll, kTxFakeDataPoll);
  EXPECT_EQ(event.macTxBeacon, kTxFakeBeacon);
  EXPECT_EQ(event.macTxBeaconReq, kTxFakeBeaconRequest);
  EXPECT_EQ(event.macTxOtherPkt, kTxFakeOther);
  EXPECT_EQ(event.macTxRetry, kTxFakeRetries);
  EXPECT_EQ(event.macTxFailCca, kTxFakeErrCca);

  EXPECT_EQ(event.phyRx, kRxFakeTotal);
  EXPECT_EQ(event.macUnicastRx, kRxFakeUnicast);
  EXPECT_EQ(event.macBroadcastRx, kRxFakeBroadcast);
  EXPECT_EQ(event.macRxData, kRxFakeData);
  EXPECT_EQ(event.macRxDataPoll, kRxFakeDataPoll);
  EXPECT_EQ(event.macRxBeacon, kRxFakeBeacon);
  EXPECT_EQ(event.macRxBeaconReq, kRxFakeBeaconRequest);
  EXPECT_EQ(event.macRxOtherPkt, kRxFakeOther);
  EXPECT_EQ(event.macRxFilterWhitelist, kRxFakeAddressFiltered);
  EXPECT_EQ(event.macRxFilterDestAddr, kRxFakeDestAddrFiltered);
  EXPECT_EQ(event.macRxFailNoFrame, kRxFakeErrNoFrame);
  EXPECT_EQ(event.macRxFailUnknownNeighbor, kRxFakeErrUnknownNeighbor);
  EXPECT_EQ(event.macRxFailInvalidSrcAddr, kRxFakeErrInvalidSrcAddr);
  EXPECT_EQ(event.macRxFailFcs, kRxFakeErrFcs);
  EXPECT_EQ(event.macRxFailOther, kRxFakeErrOther);
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningTimeout) {
  // Reset for Thread joining testing.
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_NO_ERROR);

  // Ensure timeout was set.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure params were set and provisioning monitor is bound.
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().params().is_joiner_parameter());
  const auto& params = fake_lookup_.device().provisioning_monitor().params().joiner_parameter();
  ;
  EXPECT_TRUE(params.has_pskd());
  EXPECT_TRUE(params.has_vendor_name());
  EXPECT_TRUE(params.has_vendor_model());
  EXPECT_TRUE(params.has_vendor_sw_version());

  // Advance all the way to the end.
  delegate->clock().AdvanceBy(kJoiningTimeMaxMsec);

  // Ensure provisioning progress is no longer bound and not being watched.
  EXPECT_FALSE(fake_lookup_.device().WaitForProvisioningMonitorBinding(false));
  EXPECT_FALSE(fake_lookup_.device().provisioning_monitor().WaitForWatch(false));
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningSucceeds) {
  // Reset for Thread joining testing.
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_NO_ERROR);

  // Ensure timeout was set.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure params were set and provisioning monitor is bound.
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().params().is_joiner_parameter());
  const auto& params = fake_lookup_.device().provisioning_monitor().params().joiner_parameter();
  ;
  EXPECT_TRUE(params.has_pskd());
  EXPECT_TRUE(params.has_vendor_name());
  EXPECT_TRUE(params.has_vendor_model());
  EXPECT_TRUE(params.has_vendor_sw_version());

  // Immediately complete successfully.
  fake_lookup_.device().provisioning_monitor().CompleteOk(Identity());

  // Ensure provisioning progress is no longer bound and not being watched.
  EXPECT_FALSE(fake_lookup_.device().WaitForProvisioningMonitorBinding(false));
  EXPECT_FALSE(fake_lookup_.device().provisioning_monitor().WaitForWatch(false));

  // Ensure all timers cancelled.
  EXPECT_FALSE(delegate->timeout_active());
  EXPECT_FALSE(delegate->retry_active());

  // Ensure TSM agrees it is provisioned.
  EXPECT_TRUE(ThreadStackMgrImpl()._IsThreadProvisioned());
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningRetries) {
  // Reset for Thread joining testing.
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_NO_ERROR);

  // Ensure timeout was set.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure params were set and provisioning monitor is bound.
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().params().is_joiner_parameter());
  const auto& params = fake_lookup_.device().provisioning_monitor().params().joiner_parameter();
  ;
  EXPECT_TRUE(params.has_pskd());
  EXPECT_TRUE(params.has_vendor_name());
  EXPECT_TRUE(params.has_vendor_model());
  EXPECT_TRUE(params.has_vendor_sw_version());

  // Immediately complete with a failure.
  fake_lookup_.device().provisioning_monitor().CompleteError(ProvisionError::NETWORK_NOT_FOUND);

  // Ensure provisioning progress is no longer bound and not being watched.
  EXPECT_FALSE(fake_lookup_.device().WaitForProvisioningMonitorBinding(false));
  EXPECT_FALSE(fake_lookup_.device().provisioning_monitor().WaitForWatch(false));

  // Ensure retry timer is active.
  EXPECT_EQ(delegate->clock().TimerCount(), 2u);
  EXPECT_TRUE(delegate->timeout_active());
  EXPECT_TRUE(delegate->retry_active());

  // Advance by retry delay.
  delegate->clock().AdvanceBy(kJoiningRetryDelayMsec);

  // Ensure timer fired
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningProgress) {
  // Reset for Thread joining testing.
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_NO_ERROR);

  // Ensure timeout was set.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure params were set and provisioning monitor is bound.
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().params().is_joiner_parameter());
  const auto& params = fake_lookup_.device().provisioning_monitor().params().joiner_parameter();
  ;
  EXPECT_TRUE(params.has_pskd());
  EXPECT_TRUE(params.has_vendor_name());
  EXPECT_TRUE(params.has_vendor_model());
  EXPECT_TRUE(params.has_vendor_sw_version());

  // Immediately report progress.
  fake_lookup_.device().provisioning_monitor().UpdateProgress(0.25);

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure retry timer is not active.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());
  EXPECT_FALSE(delegate->retry_active());

  // Report progress.
  fake_lookup_.device().provisioning_monitor().UpdateProgress(0.5);

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure retry timer is not active.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());
  EXPECT_FALSE(delegate->retry_active());

  // Report progress.
  fake_lookup_.device().provisioning_monitor().UpdateProgress(0.75);

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure retry timer is not active.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());
  EXPECT_FALSE(delegate->retry_active());

  // Complete successfully.
  fake_lookup_.device().provisioning_monitor().CompleteOk(Identity());

  // Ensure provisioning progress is no longer bound and not being watched.
  EXPECT_FALSE(fake_lookup_.device().WaitForProvisioningMonitorBinding(false));
  EXPECT_FALSE(fake_lookup_.device().provisioning_monitor().WaitForWatch(false));

  // Ensure all timers cancelled.
  EXPECT_FALSE(delegate->timeout_active());
  EXPECT_FALSE(delegate->retry_active());

  // Ensure TSM agrees it is provisioned.
  EXPECT_TRUE(ThreadStackMgrImpl()._IsThreadProvisioned());
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningCanceled) {
  // Reset for Thread joining testing.
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_NO_ERROR);

  // Ensure timeout was set.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure params were set and provisioning monitor is bound.
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().params().is_joiner_parameter());
  const auto& params = fake_lookup_.device().provisioning_monitor().params().joiner_parameter();
  ;
  EXPECT_TRUE(params.has_pskd());
  EXPECT_TRUE(params.has_vendor_name());
  EXPECT_TRUE(params.has_vendor_model());
  EXPECT_TRUE(params.has_vendor_sw_version());

  // Immediately complete with a failure.
  fake_lookup_.device().provisioning_monitor().CompleteError(ProvisionError::CANCELED);

  // Ensure provisioning progress is no longer bound and not being watched.
  EXPECT_FALSE(fake_lookup_.device().WaitForProvisioningMonitorBinding(false));
  EXPECT_FALSE(fake_lookup_.device().provisioning_monitor().WaitForWatch(false));

  // Ensure timers are canceled.
  EXPECT_FALSE(delegate->timeout_active());
  EXPECT_FALSE(delegate->retry_active());
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningTimeoutDoesNotCancelInProgressJoin) {
  // Reset for Thread joining testing.
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_NO_ERROR);

  // Ensure timeout was set.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure params were set and provisioning monitor is bound.
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().params().is_joiner_parameter());
  const auto& params = fake_lookup_.device().provisioning_monitor().params().joiner_parameter();
  ;
  EXPECT_TRUE(params.has_pskd());
  EXPECT_TRUE(params.has_vendor_name());
  EXPECT_TRUE(params.has_vendor_model());
  EXPECT_TRUE(params.has_vendor_sw_version());

  // Advance until just before timeout
  delegate->clock().AdvanceBy(kJoiningTimeMaxMsec - (kJoiningRetryDelayMsec / 2));

  // Report progress
  fake_lookup_.device().provisioning_monitor().UpdateProgress(0.25);

  // Ensure provisioning progress is bound and being watched.
  EXPECT_TRUE(fake_lookup_.device().WaitForProvisioningMonitorBinding(true));
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().WaitForWatch(true));

  // Ensure timeout is still armed.
  EXPECT_EQ(delegate->clock().TimerCount(), 1u);
  EXPECT_TRUE(delegate->timeout_active());

  // Advance until after timeout.
  delegate->clock().AdvanceBy(kJoiningRetryDelayMsec / 2);

  // Confirm timeout fired.
  EXPECT_EQ(delegate->clock().TimerCount(), 0u);

  // Ensure provisioning progress is bound and still being watched.
  EXPECT_TRUE(fake_lookup_.device().is_provisioning_monitor_bound());
  EXPECT_TRUE(fake_lookup_.device().provisioning_monitor().IsWatched());

  // Complete successfully.
  fake_lookup_.device().provisioning_monitor().CompleteOk(Identity());

  // Ensure provisioning progress is no longer bound and not being watched.
  EXPECT_FALSE(fake_lookup_.device().WaitForProvisioningMonitorBinding(false));
  EXPECT_FALSE(fake_lookup_.device().provisioning_monitor().WaitForWatch(false));

  // Ensure all timers cancelled.
  EXPECT_FALSE(delegate->timeout_active());
  EXPECT_FALSE(delegate->retry_active());

  // Ensure TSM agrees it is provisioned.
  EXPECT_TRUE(ThreadStackMgrImpl()._IsThreadProvisioned());
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningMissingPairingCode) {
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  ResetConfigMgr();
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningMissingVendorName) {
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  configuration_delegate().set_vendor_id_description(std::nullopt);
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningMissingProductName) {
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  configuration_delegate().set_product_id_description(std::nullopt);
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

// Temporarily disabled due to flake.
TEST_F(ThreadStackManagerTest, DISABLED_StartupThreadJoiningMissingFirmwareVersion) {
  ThreadStackMgrImpl().SetDelegate(nullptr);
  fake_lookup_.device().provisioning_monitor().Reset();
  configuration_delegate().set_firmware_revision(std::nullopt);
  auto delegate = new MockedTimerThreadStackManagerDelegateImpl();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerDelegateImpl>(delegate));
  EXPECT_EQ(ThreadStackMgr().InitThreadStack(), WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

}  // namespace testing
}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
