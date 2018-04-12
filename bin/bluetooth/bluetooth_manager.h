// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_BLUETOOTH_ADAPTER_MANAGER_H_
#define GARNET_BIN_BLUETOOTH_ADAPTER_MANAGER_H_

#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include <fuchsia/cpp/bluetooth_control.h>
#include <fuchsia/cpp/bluetooth_host.h>
#include <lib/async/cpp/task.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/io/device_watcher.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth_service {

class Adapter;
class BluetoothManager;

// BluetoothManager is responsible for managing the general Bluetooth status of
// the system. Specifically, it
//
//   * acts as the backend for the control.Control interface;
//
//   * maintains a connection to every bt-host device that is on the system;
//
//   * is responsible for routing generic requests to specific Adapters, and
//     the current idle mode of each adapter (discoverable, connectable, etc)
//
//   * buffers requests during service startup so early FIDL requests don't
//     fail prematurely.
//
// INITIALIZATION:
//
// BluetoothManager starts out in the "initializing" state. It provides
// asynchronous information calls which are resolved when the system is ready.
//
// BluetoothManager moves out of the "initializing" state once the first bt-host
// is initialized or after 5 seconds if no bt-host devices are found.
class DiscoveryRequestToken {
 public:
  // Destroying a Discovery Reqeust Token cancels the request.
  ~DiscoveryRequestToken();

 private:
  friend class BluetoothManager;

  // Called by BluetoothManager
  explicit DiscoveryRequestToken(fxl::WeakPtr<BluetoothManager> vendor);

  // A weak pointer to the manager who vended this token.
  fxl::WeakPtr<BluetoothManager> vendor_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DiscoveryRequestToken);
};

class BluetoothManager final {
 public:
  using ActiveAdapterCallback = std::function<void(const Adapter*)>;
  using AdapterInfoCallback =
      std::function<void(const bluetooth_control::AdapterInfoPtr&)>;
  using AdapterRemovedCallback = std::function<void(const std::string&)>;

  using AdapterInfoMap =
      std::unordered_map<std::string, bluetooth_control::AdapterInfo>;
  using AdapterInfoMapCallback = std::function<void(const AdapterInfoMap&)>;

  using DiscoveryRequestCallback =
      std::function<void(std::unique_ptr<DiscoveryRequestToken> token,
                         const std::string& reason)>;

  using RemoteDeviceUpdatedCallback =
      std::function<void(const bluetooth_control::RemoteDevice&)>;

  BluetoothManager();
  ~BluetoothManager();

  // Called when the active adapter changes with a pointer to the new active
  // adapter's information. Called with nullptr if an active adapter no longer
  // exists.
  void set_active_adapter_changed_callback(AdapterInfoCallback callback) {
    active_adapter_changed_cb_ = std::move(callback);
  }

  // Called when an adapter is updated
  void set_adapter_updated_callback(AdapterInfoCallback callback) {
    adapter_updated_cb_ = std::move(callback);
  }

  // Called when an adapter is removed.
  void set_adapter_removed_callback(AdapterRemovedCallback callback) {
    adapter_removed_cb_ = std::move(callback);
  }

  // Asynchronously returns a Host interface pointer to the current active
  // adapter when the BluetoothManager becomes initialized.  Returns nullptr if
  // there is no active adapter.
  void GetActiveAdapter(ActiveAdapterCallback callback);

  // Asynchronously returns the info for known adapters when the
  // BluetoothManager becomes initialized.
  void GetKnownAdapters(AdapterInfoMapCallback callback);

  // Makes the adapter with the given |identifier| the active adapter. Returns
  // false if |identifier| is not recognized or if the BluetoothManager has not
  // been fully initialized.
  bool SetActiveAdapter(const std::string& identifier);

  // Requests discovery to be active.  Calls |callback| when the
  // request is complete with a token which should be relinquished when
  // discovery is not requested anymore, or nullptr if it is not possible
  // to request discovery, and a reason.
  void RequestDiscovery(DiscoveryRequestCallback callback);

  // Sets a callback to receive ongoing updates about remote devices.
  void set_device_updated_callback(RemoteDeviceUpdatedCallback callback) {
    device_updated_cb_ = std::move(callback);
  }

 private:
  friend class DiscoveryRequestToken;

  // All currently known adapters.
  const AdapterInfoMap GetAdapterInfoMap() const;

  // Called by |device_watcher_| when bt-host devices are found.
  void OnHostFound(int dir_fd, std::string filename);

  // Called when an Adapter is ready to be created. This creates and stores an
  // Adapter with the given parameters. If this is the first adapter that is
  // created then it will be assigned as the new active adapter.
  //
  // This also causes this BluetoothManager to transition out of the
  // "initializing" state (if it is in that state) and resolve all adapter
  // requests that were previously queued.
  void CreateAdapter(bluetooth_host::HostPtr host,
                     bluetooth_control::AdapterInfo info);

  // Called when the connection to a Host is lost.
  void OnHostDisconnected(const std::string& identifier);

  // Called when a remote device has updated.
  void OnRemoteDeviceUpdated(const bluetooth_control::RemoteDevice& device);

  // Called when |init_timeout_task_| expires.
  void OnInitTimeout(async_t* async, async::TaskBase* task, zx_status_t status);

  // Cancels |init_timeout_task_|.
  void CancelInitTimeout();

  // Assigns |adapter| as active. If there is a current active adapter then it
  // will be told to close all of its handles.
  void SetActiveAdapterInternal(Adapter* adapter);

  // Marks this instance as initialized and resolves all pending requests.
  void ResolvePendingRequests();

  // Removes the discovery |token| and possibly stops discovery.
  void RemoveDiscoveryRequest(DiscoveryRequestToken* token);

  // An BluetoothManager is in the "initializing" state when it gets created and
  // remains in this state until the first local adapter it processes or when a
  // timer expires.
  bool initializing_;

  AdapterInfoCallback active_adapter_changed_cb_;
  AdapterInfoCallback adapter_updated_cb_;
  AdapterRemovedCallback adapter_removed_cb_;
  RemoteDeviceUpdatedCallback device_updated_cb_;

  // Used to monitor bt-host devices.
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  // Mapping from adapter IDs to Adapters.
  std::unordered_map<std::string, std::unique_ptr<Adapter>> adapters_;

  // The currently active adapter. This raw pointer points to a managed instance
  // stored in |adapters_|.
  Adapter* active_adapter_;  // weak

  // The initializing state timeout. We use this to exit the "initializing"
  // state if no adapters are added during this period.
  async::TaskMethod<BluetoothManager,
                    &BluetoothManager::OnInitTimeout> init_timeout_task_{this};

  // Asynchronous requests queued during the "initializing" state.
  std::queue<std::function<void()>> pending_requests_;

  // The currently active discovery requests.
  // Discovery should be active when this is non-empty.
  std::unordered_set<DiscoveryRequestToken*> discovery_requests_;

  // Vends weak pointers. This is kept as the last member so that, upon
  // destruction, weak pointers are invalidated before other members are
  // destroyed.
  fxl::WeakPtrFactory<BluetoothManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BluetoothManager);
};

// Represents a local Bluetooth adapter backed by a bt-host device. Instances of
// this class are owned by a BluetoothManager.
class Adapter : public bluetooth_host::AdapterDelegate {
 public:
  // Returns a basic information about this adapter, such as its ID and address.
  const bluetooth_control::AdapterInfo& info() const { return info_; }

  // Returns a Host interface pointer that can be used to send messages to the
  // underlying bt-host. Returns nullptr if the Host interface pointer is not
  // bound.
  bluetooth_host::Host* host() const { return host_ ? host_.get() : nullptr; }

 private:
  friend class BluetoothManager;

  Adapter(bluetooth_control::AdapterInfo info,
          bluetooth_host::HostPtr host,
          BluetoothManager::RemoteDeviceUpdatedCallback update_cb);

  void StartDiscovery(
      std::unique_ptr<DiscoveryRequestToken> token,
      BluetoothManager::DiscoveryRequestCallback callback) const;
  void StopDiscovery() const;

  // ::bluetooth_host::AdapterDelegate overrides:
  void OnAdapterStateChanged(bluetooth_control::AdapterState state) override;
  void OnDeviceDiscovered(bluetooth_control::RemoteDevice device) override;

  // A cached version of the info for this adapter.
  bluetooth_control::AdapterInfo info_;

  // The Host interface that is owned.
  bluetooth_host::HostPtr host_;

  // Adapter handles used to receive updates about adapter state and control
  // discovery.
  bluetooth_host::AdapterPtr host_adapter_;
  fidl::Binding<bluetooth_host::AdapterDelegate> adapter_delegate_;

  // Update callback called when a device is discovered.
  BluetoothManager::RemoteDeviceUpdatedCallback update_cb_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Adapter);
};

}  // namespace bluetooth_service

#endif  // GARNET_BIN_BLUETOOTH_ADAPTER_MANAGER_H_
