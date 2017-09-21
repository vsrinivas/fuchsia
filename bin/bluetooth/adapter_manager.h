// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "garnet/drivers/bluetooth/lib/common/observer_list.h"

#include "lib/fsl/io/device_watcher.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth {
namespace gap {

class Adapter;

}  // namespace gap
}  // namespace bluetooth

namespace bluetooth_service {

// AdapterManager is a singleton that is responsible for initializing, cleaning
// up, and providing access to Adapter instances.
//
// This class is not thread-safe.
class AdapterManager final {
 public:
  class Observer {
   public:
    virtual ~Observer() = default;

    // Called when the active adapter changes. |adapter| will be nullptr if all
    // adapters have been removed and no new default was set.
    virtual void OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) = 0;

    // Called when a new Bluetooth HCI device is found. This will be called with
    // a fully initialized Adapter instance.
    virtual void OnAdapterCreated(bluetooth::gap::Adapter* adapter);

    // Called when a Bluetooth HCI device has been removed from the system or
    // any of the transport channels was shut down for an unknown reason. The
    // returned adapter will have been completely shut down and is ready for
    // removal.
    virtual void OnAdapterRemoved(bluetooth::gap::Adapter* adapter);
  };

  AdapterManager();
  ~AdapterManager();

  // Returns the adapter with the given |identifier|. Returns nullptr if
  // |identifier| is not recognized.
  fxl::WeakPtr<bluetooth::gap::Adapter> GetAdapter(
      const std::string& identifier) const;

  // Calls the given iterator function over all currently known adapters.
  using ForEachAdapterFunc = std::function<void(bluetooth::gap::Adapter*)>;
  void ForEachAdapter(const ForEachAdapterFunc& func) const;

  // Returns true if any Bluetooth adapters are currently managed by this
  // AdapterManager.
  bool HasAdapters() const;

  // Adds/Removes an Observer to receive Adapter life-cycle notifications from
  // us. Each registered |observer| MUST out-live this AdapterManager.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Returns the current active adapter. Returns nullptr if no active adapter
  // was set.
  fxl::WeakPtr<bluetooth::gap::Adapter> GetActiveAdapter();

  // Assigns the current active adapter. Returns false if |identifier| is not
  // recognized. Otherwise notifies all observers and returns true.
  bool SetActiveAdapter(const std::string& identifier);

 private:
  bool SetActiveAdapterInternal(bluetooth::gap::Adapter* adapter);

  // Called by |device_watcher_| for Bluetooth HCI devices that are found on the
  // system.
  void OnDeviceFound(int dir_fd, std::string filename);

  // Called after an Adapter is initialized.
  void RegisterAdapter(std::unique_ptr<bluetooth::gap::Adapter> adapter);

  // Called when an adapter object's underlying transport gets closed.
  void OnAdapterTransportClosed(std::string adapter_identifier);

  // Called by OnAdapterTransportClosed when the current active adapter has been
  // removed. This makes the next available adapter as active, or sets the
  // active adapter to nullptr if none exists.
  void AssignNextActiveAdapter();

  // The list of observers who are interested in notifications from us.
  ::bluetooth::common::ObserverList<Observer> observers_;

  // The device watcher we use to watch for Bluetooth HCI devices in the system.
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  // All Adapter instances that we are managing.
  std::unordered_map<std::string, std::unique_ptr<bluetooth::gap::Adapter>>
      adapters_;

  // The current active adapter.
  bluetooth::gap::Adapter* active_adapter_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  fxl::WeakPtrFactory<AdapterManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AdapterManager);
};

}  // namespace bluetooth_service
