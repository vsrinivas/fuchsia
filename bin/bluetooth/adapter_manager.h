// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>
#include <unordered_map>

#include <async/cpp/task.h>

#include "lib/bluetooth/fidl/control.fidl.h"
#include "lib/fsl/io/device_watcher.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/lib/bluetooth/fidl/host.fidl.h"

namespace bluetooth_service {

// Represents a local Bluetooth adapter backed by a bt-host device. Instances of
// this class are owned by an AdapterManager.
class Adapter {
 public:
  // Returns a basic information about this adapter, such as its ID and address.
  const bluetooth::control::AdapterInfoPtr& info() const { return info_; }

  // Returns a Host interface pointer that can be used to send messages to the
  // underlying bt-host. Returns nullptr if the Host interface pointer is not
  // bound.
  bluetooth::host::Host* host() const { return host_ ? host_.get() : nullptr; }

 private:
  friend class AdapterManager;

  Adapter(bluetooth::control::AdapterInfoPtr info,
          bluetooth::host::HostPtr host);

  bluetooth::control::AdapterInfoPtr info_;
  bluetooth::host::HostPtr host_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Adapter);
};

// AdapterManager is responsible for managing the Bluetooth adapters on the
// system. Specifically, it
//
//   * acts as the backend for the control.AdapterManager interface;
//
//   * tracks and maintains a connection to every bt-host device that is on the
//     system;
//
//   * is responsible for maintaining the active adapter and making sure
//     inactive adapters are disabled when not in use;
//
//   * buffers requests to access the adapters during bootstrap so that early
//     FIDL requests don't fail prematurely.
//
// INITIALIZATION:
//
// AdapterManager starts out in the "initializing" state. It provides
// asynchronous getters for the adapters which get resolved when AdapterManager
// becomes fully initialized.
//
// AdapterManager moves out of the "initializing" state once the first bt-host
// is initialized or after 5 seconds if no bt-host devices are found during that
// time.
class AdapterManager final {
 public:
  using ActiveAdapterCallback = std::function<void(const Adapter*)>;
  using AdapterCallback = std::function<void(const Adapter&)>;

  using AdapterMap = std::unordered_map<std::string, std::unique_ptr<Adapter>>;
  using AdapterMapCallback = std::function<void(const AdapterMap&)>;

  AdapterManager();
  ~AdapterManager();

  // Called when the active adapter changes with a pointer to the new active
  // adapter's information. Called with nullptr if an active adapter no longer
  // exists.
  //
  // NOTE: The provided Adapter is owned by this AdapterManager. The given raw
  // pointer should not be retained.
  void set_active_adapter_changed_callback(ActiveAdapterCallback callback) {
    active_adapter_changed_cb_ = std::move(callback);
  }

  // Called when an adapter is added.
  void set_adapter_added_callback(AdapterCallback callback) {
    adapter_added_cb_ = std::move(callback);
  }

  // Called when an adapter is removed.
  void set_adapter_removed_callback(AdapterCallback callback) {
    adapter_removed_cb_ = std::move(callback);
  }

  // Asynchronously returns a Host interface pointer to the current active
  // adapter when the AdapterManager becomes initialzed. Returns nullptr if
  // there is no active adapter.
  void GetActiveAdapter(ActiveAdapterCallback callback);

  // Asynchronously returns the list of known adapters when the AdapterManager
  // becomes initialized.
  void ListAdapters(AdapterMapCallback callback);

  // Makes the adapter with the given |identifier| the active adapter. Returns
  // false if |identifier| is not recognized or if the AdapterManager has not
  // been fully initialized.
  bool SetActiveAdapter(const std::string& identifier);

  // Synchronously returns the current active adapter.
  Adapter* active_adapter() const { return active_adapter_; }

  // All currently known adapters.
  const AdapterMap& adapters() const { return adapters_; }

 private:
  // Called by |device_watcher_| when bt-host devices are found.
  void OnDeviceFound(int dir_fd, std::string filename);

  // Called when an Adapter is ready to be created. This creates and stores an
  // Adapter with the given parameters. If this is the first adapter that is
  // created then it will be assigned as the new active adapter.
  //
  // This also causes this AdapterManager to transition out of the
  // "initializing" state (if it is in that state) and resolve all adapter
  // requests that were previously queued.
  void CreateAdapter(bluetooth::host::HostPtr host,
                     bluetooth::control::AdapterInfoPtr info);

  // Called when the connection to a Host is lost.
  void OnHostDisconnected(const std::string& identifier);

  // Called when |init_timeout_task_| expires.
  async_task_result_t OnInitTimeout(async_t* async, zx_status_t status);

  // Cancels |init_timeout_task_|.
  void CancelInitTimeout();

  // Assigns |adapter| as active. If there is a current active adapter then it
  // will be told to close all of its handles.
  void SetActiveAdapterInternal(Adapter* adapter);

  // Marks this instance as initialized and resolves all pending requests.
  void ResolvePendingRequests();

  // An AdapterManager is in the "initializing" state when it gets created and
  // remains in this state until the first local adapter it processes or when a
  // timer expires.
  bool initializing_;

  ActiveAdapterCallback active_adapter_changed_cb_;
  AdapterCallback adapter_added_cb_;
  AdapterCallback adapter_removed_cb_;

  // Used to monitor bt-host devices.
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  // Mapping from adapter IDs to Adapters.
  AdapterMap adapters_;

  // The currently active adapter. This raw pointer points to a managed instance
  // stored in |adapters_|.
  Adapter* active_adapter_;  // weak

  // The initializing state timeout. We use this to exit the "initializing"
  // state if no adapters are added during this period.
  async::Task init_timeout_task_;

  // Asynchronous requests queued during the "initializing" state.
  std::queue<std::function<void()>> pending_requests_;

  // Vends weak pointers. This is kept as the last member so that, upon
  // destruction, weak pointers are invalidated before other members are
  // destroyed.
  fxl::WeakPtrFactory<AdapterManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AdapterManager);
};

}  // namespace bluetooth_service
