// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include "garnet/drivers/bluetooth/lib/gap/adapter_state.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_connection_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace bluetooth {

namespace hci {
class DeviceWrapper;
class SequentialCommandRunner;
class Transport;
}  // namespace hci

namespace l2cap {
class ChannelManager;
}  // namespace l2cap

namespace gap {

class LowEnergyAdvertisingManager;
class LowEnergyDiscoveryManager;

// Represents the host-subsystem state for a Bluetooth controller. All
// asynchronous callbacks are posted on the MessageLoop on which this Adapter
// instances is created.
//
// This class is not thread-safe and it is intended to be created, deleted, and
// accessed on the same event loop. No internal locking is provided.
//
// NOTE: We currently only support primary controllers. AMP controllers are not
// supported.
class Adapter final {
 public:
  // A fsl::MessageLoop must have been initialized when an Adapter instance is
  // created. The Adapter instance will use the MessageLoop it is created on for
  // all of its asynchronous tasks.
  //
  // This will take ownership of |hci_device|.
  explicit Adapter(std::unique_ptr<hci::DeviceWrapper> hci_device);
  ~Adapter();

  // Returns a 128-bit UUID that uniquely identifies this adapter on the current
  // system.
  std::string identifier() const { return identifier_; }

  // Initializes the host-subsystem state for the HCI device this was created
  // for. This performs the initial HCI transport set up. Returns false if an
  // immediate error occurs. Otherwise this returns true and asynchronously
  // notifies the caller on the initialization status via |callback|.
  //
  // After successful initialization, |transport_closed_callback| will be
  // invoked when the underlying HCI transport closed for any reason (e.g. the
  // device disappeared or the transport channels were closed for an unknown
  // reason). The implementation is responsible for cleaning up this adapter by
  // calling ShutDown().
  using InitializeCallback = std::function<void(bool success)>;
  bool Initialize(const InitializeCallback& callback,
                  const fxl::Closure& transport_closed_callback);

  // Shuts down this Adapter. Invokes |callback| when shut down has completed.
  // TODO(armansito): This needs to do several things to potentially preserve
  // the state of various sub-protocols. For now we keep the interface pretty
  // simple.
  void ShutDown();

  // Returns true if the Initialize() sequence has started but not completed yet
  // (i.e. the InitializeCallback that was passed to Initialize() has not yet
  // been called).
  bool IsInitializing() const { return init_state_ == State::kInitializing; }

  // Returns true if this Adapter has been fully initialized.
  bool IsInitialized() const { return init_state_ == State::kInitialized; }

  // Returns the global adapter setting parameters.
  const AdapterState& state() const { return state_; }

  // Returns a weak pointer to this adapter.
  fxl::WeakPtr<Adapter> AsWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

  // Returns this Adapter's remote device cache.
  const RemoteDeviceCache& device_cache() const { return device_cache_; }

  // Returns this Adapter's LE discovery manager.
  LowEnergyDiscoveryManager* le_discovery_manager() const {
    FXL_DCHECK(le_discovery_manager_);
    return le_discovery_manager_.get();
  }

  // Returns this Adapter's LE connection manager.
  LowEnergyConnectionManager* le_connection_manager() const {
    FXL_DCHECK(le_connection_manager_);
    return le_connection_manager_.get();
  }

  // Returns this Adapter's LE advertising manager.
  LowEnergyAdvertisingManager* le_advertising_manager() const {
    FXL_DCHECK(le_advertising_manager_);
    return le_advertising_manager_.get();
  }

 private:
  // Second step of the initialization sequence. Called by Initialize() when the
  // first batch of HCI commands have been sent.
  void InitializeStep2(const InitializeCallback& callback);

  // Third step of the initialization sequence. Called by InitializeStep2() when
  // the second batch of HCI commands have been sent.
  void InitializeStep3(const InitializeCallback& callback);

  // Fourth step of the initialization sequence. Called by InitializeStep3()
  // when the third batch of HCI commands have been sent.
  void InitializeStep4(const InitializeCallback& callback);

  // Builds and returns the HCI event mask based on our supported host side
  // features and controller capabilities. This is used to mask events that we
  // do not know how to handle.
  uint64_t BuildEventMask();

  // Builds and returns the LE event mask based on our supported host side
  // features and controller capabilities. This is used to mask LE events that
  // we do not know how to handle.
  uint64_t BuildLEEventMask();

  // Called by ShutDown() and during Initialize() in case of failure. This
  // synchronously cleans up the transports and resets initialization state.
  void CleanUp();

  // Called by Transport after it has been unexpectedly closed.
  void OnTransportClosed();

  // Uniquely identifies this adapter on the current system.
  std::string identifier_;

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  fxl::RefPtr<hci::Transport> hci_;

  // Callback invoked to notify clients when the underlying transport is closed.
  fxl::Closure transport_closed_cb_;

  // Parameters relevant to the initialization sequence.
  // TODO(armansito): The Initialize()/ShutDown() pattern has become common
  // enough in this project that it might be worth considering moving the
  // init-state-keeping into an abstract base.
  enum State {
    kNotInitialized = 0,
    kInitializing,
    kInitialized,
  };
  std::atomic<State> init_state_;
  std::unique_ptr<hci::SequentialCommandRunner> init_seq_runner_;

  // Contains the global adapter state.
  AdapterState state_;

  // Provides access to discovered, connected, and/or bonded remote Bluetooth
  // devices.
  RemoteDeviceCache device_cache_;

  // The L2CAP layer.
  std::unique_ptr<l2cap::ChannelManager> l2cap_;

  // Interface for performing BLE scan procedures. This is initialized based on
  // feature support. Contains nullptr if the controller does not support
  // scanning.
  std::unique_ptr<LowEnergyDiscoveryManager> le_discovery_manager_;

  // Interface for BLE central-role connection procedures. This is initialized
  // based on feature support. Contains nullptr if the controller does not
  // support LE connections.
  std::unique_ptr<LowEnergyConnectionManager> le_connection_manager_;
  LowEnergyConnectionManager::ListenerId incoming_listener_id_;

  // Interface for performing BLE advertisement procedures.  Initialized based
  // on feature support.  nullptr when a controller does not support
  // advertising.
  std::unique_ptr<LowEnergyAdvertisingManager> le_advertising_manager_;

  // This must remain the last member to make sure that all weak pointers are
  // invalidating before other members are destroyed.
  fxl::WeakPtrFactory<Adapter> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Adapter);
};

}  // namespace gap
}  // namespace bluetooth
