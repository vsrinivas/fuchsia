// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_THREAD_STACK_MANAGER_DELEGATE_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_THREAD_STACK_MANAGER_DELEGATE_IMPL_H_

#include <fuchsia/lowpan/device/cpp/fidl.h>
#include <fuchsia/lowpan/thread/cpp/fidl.h>
#include <lib/async/cpp/task.h>

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
#pragma GCC diagnostic pop
// clang-format on

#include "src/lib/backoff/exponential_backoff.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {

class NL_DLL_EXPORT ThreadStackManagerDelegateImpl : public ThreadStackManagerImpl::Delegate {
 public:
  ThreadStackManagerDelegateImpl();

  // ThreadStackManager implementations.

  WEAVE_ERROR InitThreadStack() override;
  bool HaveRouteToAddress(const IPAddress& destAddr) override;
  void OnPlatformEvent(const WeaveDeviceEvent* event) override;
  bool IsThreadEnabled() override;
  WEAVE_ERROR SetThreadEnabled(bool val) override;
  bool IsThreadProvisioned() override;
  bool IsThreadAttached() override;
  WEAVE_ERROR GetThreadProvision(Internal::DeviceNetworkInfo& netInfo,
                                 bool includeCredentials) override;
  WEAVE_ERROR SetThreadProvision(const Internal::DeviceNetworkInfo& netInfo) override;
  void ClearThreadProvision() override;
  ConnectivityManager::ThreadDeviceType GetThreadDeviceType() override;
  bool HaveMeshConnectivity() override;
  WEAVE_ERROR GetAndLogThreadStatsCounters() override;
  WEAVE_ERROR GetAndLogThreadTopologyMinimal() override;
  WEAVE_ERROR GetAndLogThreadTopologyFull() override;
  std::string GetInterfaceName() const override;
  bool IsThreadSupported() const override;
  WEAVE_ERROR GetPrimary802154MACAddress(uint8_t* mac_address) override;
  WEAVE_ERROR SetThreadJoinable(bool enable) override;

  nl::Weave::Profiles::DataManagement::event_id_t LogNetworkWpanStatsEvent(
      Schema::Nest::Trait::Network::TelemetryNetworkWpanTrait::NetworkWpanStatsEvent* event)
      override;

 private:
  std::string interface_name_;
  bool is_thread_supported_ = false;

  // Handle events from fuchsia.lowpan.device to maintain Thread state.
  void WatchLowpanDeviceChange();
  void OnLowpanDeviceChange(fuchsia::lowpan::device::DeviceChanges changes);

  zx_status_t GetProtocols(fuchsia::lowpan::device::Protocols protocols);
  zx_status_t GetDevice(fuchsia::lowpan::device::DeviceSyncPtr& device);
  zx_status_t GetDeviceState(fuchsia::lowpan::device::DeviceState& device_state);

  // Thread-joining-on-startup related functionality and state.
  WEAVE_ERROR StartThreadJoining();
  WEAVE_ERROR StartThreadJoiningIteration();
  void StopThreadJoining();
  WEAVE_ERROR GetJoinParams(fuchsia::lowpan::JoinParams& join_params);
  virtual WEAVE_ERROR StartJoiningTimeout(uint32_t delay_milliseconds,
                                          fit::function<void()> callback);
  virtual WEAVE_ERROR StartJoiningRetry(uint32_t delay_milliseconds,
                                        fit::function<void()> callback);
  virtual void CancelJoiningTimeout();
  virtual void CancelJoiningRetry();
  void HandleJoiningTimeout();
  void HandleJoiningRetryDelay();
  void HandleProvisioningProgress(
      fuchsia::lowpan::device::ProvisioningMonitor_WatchProgress_Result);

  async::TaskClosure lookup_watcher_delayed_task_;
  backoff::ExponentialBackoff lookup_watcher_backoff_;
  fuchsia::lowpan::device::LookupPtr lookup_watcher_;

  async::TaskClosure joining_timeout_;
  async::TaskClosure joining_retry_delay_;
  fuchsia::lowpan::device::ProvisioningMonitorPtr provisioning_monitor_;

  bool joining_in_progress_ = false;
  bool joining_timeout_expired_ = false;
};

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_THREAD_STACK_MANAGER_DELEGATE_IMPL_H_
