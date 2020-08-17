// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_THREAD_STACK_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_THREAD_STACK_MANAGER_IMPL_H_

#include <fuchsia/lowpan/device/cpp/fidl.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {

class ThreadStackManagerImpl final : public ThreadStackManager {
  // Allow the ThreadStackManager interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend class ThreadStackManager;

  // Allow the singleton accessors to access the instance.
  friend ThreadStackManager& ::nl::Weave::DeviceLayer::ThreadStackMgr();
  friend ThreadStackManagerImpl& ::nl::Weave::DeviceLayer::ThreadStackMgrImpl();

 private:
  ThreadStackManagerImpl() = default;

  // ThreadStackManager implementations. Public for testing purposes only.
 public:
  WEAVE_ERROR _InitThreadStack();

  void _ProcessThreadActivity() {}
  WEAVE_ERROR _StartThreadTask() {
    return WEAVE_NO_ERROR;  // No thread task is managed here.
  }

  void _LockThreadStack() {}
  bool _TryLockThreadStack() { return true; }
  void _UnlockThreadStack() {}

  bool _HaveRouteToAddress(const IPAddress& destAddr);

  WEAVE_ERROR _GetPrimary802154MACAddress(uint8_t* buf);

  void _OnPlatformEvent(const WeaveDeviceEvent* event);

  bool _IsThreadEnabled();
  WEAVE_ERROR _SetThreadEnabled(bool val);
  bool _IsThreadProvisioned();
  bool _IsThreadAttached();

  WEAVE_ERROR _GetThreadProvision(Internal::DeviceNetworkInfo& netInfo, bool includeCredentials);
  WEAVE_ERROR _SetThreadProvision(const Internal::DeviceNetworkInfo& netInfo);
  void _ClearThreadProvision();

  ConnectivityManager::ThreadDeviceType _GetThreadDeviceType();
  WEAVE_ERROR _SetThreadDeviceType(ConnectivityManager::ThreadDeviceType threadRole) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }

  void _GetThreadPollingConfig(ConnectivityManager::ThreadPollingConfig& pollingConfig) {
    pollingConfig.Clear();  // GetThreadPollingConfig not supported.
  }
  WEAVE_ERROR _SetThreadPollingConfig(
      const ConnectivityManager::ThreadPollingConfig& pollingConfig) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }

  bool _HaveMeshConnectivity();

  void _OnMessageLayerActivityChanged(bool messageLayerIsActive) {}

  void _OnWoBLEAdvertisingStart() {}
  void _OnWoBLEAdvertisingStop() {}

  WEAVE_ERROR _GetAndLogThreadStatsCounters();
  WEAVE_ERROR _GetAndLogThreadTopologyMinimal();
  WEAVE_ERROR _GetAndLogThreadTopologyFull();

 private:
  static ThreadStackManagerImpl sInstance;
  std::string interface_name_;

  fuchsia::lowpan::device::DeviceSyncPtr device_;

  zx_status_t GetProtocols(fuchsia::lowpan::device::Protocols protocols);
  zx_status_t GetDeviceState(fuchsia::lowpan::device::DeviceState* state);
};

inline ThreadStackManager& ThreadStackMgr() { return ThreadStackManagerImpl::sInstance; }
inline ThreadStackManagerImpl& ThreadStackMgrImpl() { return ThreadStackManagerImpl::sInstance; }

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_THREAD_STACK_MANAGER_IMPL_H_
