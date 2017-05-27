// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include "apps/bluetooth/lib/gap/adapter_state.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace bluetooth {

namespace hci {
class DeviceWrapper;
class SequentialCommandRunner;
class Transport;
}  // namespace hci

namespace gap {

// Represents the host-subsystem state for a Bluetooth controller. All asynchronous callbacks are
// posted on the MessageLoop on which this Adapter instances is created.
//
// This class is not thread-safe and it is intended to be created, deleted, and accessed on the same
// event loop. No internal locking is provided.
//
// NOTE: We currently only support primary controllers. AMP controllers are not supported.
class Adapter final {
 public:
  // A mtl::MessageLoop must have been initialized when an Adapter instance is created. The Adapter
  // instance will use the MessageLoop it is created on for all of its asynchronous tasks.
  //
  // This will take ownership of |hci_device|.
  explicit Adapter(std::unique_ptr<hci::DeviceWrapper> hci_device);
  ~Adapter();

  // Returns a 128-bit UUID that uniquely identifies this adapter on the current system.
  std::string identifier() const { return identifier_; }

  // Initializes the host-subsystem state for the HCI device this was created for. This performs
  // the initial HCI transport set up. Returns false if an immediate error occurs. Otherwise this
  // returns true and asynchronously notifies the caller on the initialization status via
  // |callback|.
  //
  // After successful initialization, |transport_closed_callback| will be invoked when the
  // underlying HCI transport closed for any reason (e.g. the device disappeared or the transport
  // channels were closed for an unknown reason). The implementation is responsible for cleaning up
  // this adapter by calling ShutDown().
  using InitializeCallback = std::function<void(bool success)>;
  bool Initialize(const InitializeCallback& callback,
                  const ftl::Closure& transport_closed_callback);

  // Shuts down this Adapter. Invokes |callback| when shut down has completed.
  // TODO(armansito): This needs to do several things to potentially preserve the state of various
  // sub-protocols. For now we keep the interface pretty simple.
  void ShutDown();

  // Returns true if the Initialize() sequence has started but not completed yet (i.e. the
  // InitializeCallback that was passed to Initialize() has not yet been called).
  bool IsInitializing() const { return init_state_ == State::kInitializing; }

  // Returns true if this Adapter has been fully initialized.
  bool IsInitialized() const { return init_state_ == State::kInitialized; }

  // Returns the global adapter setting parameters.
  const AdapterState& state() const { return state_; }

  // Returns a weak pointer to this adapter.
  ftl::WeakPtr<Adapter> AsWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

 private:
  // Second step of the initialization sequence. Called by Initialize() when the first batch of HCI
  // commands have been sent.
  void InitializeStep2(const InitializeCallback& callback);

  // Third step of the initialization sequence. Called by InitializeStep2() when the second batch of
  // HCI commands have been sent.
  void InitializeStep3(const InitializeCallback& callback);

  // Builds and returns the HCI event mask based on our supported host side features and controller
  // capabilities. This is used to mask events that we do not know how to handle.
  uint64_t BuildEventMask();

  // Builds and returns the LE event mask based on our supported host side features and controller
  // capabilities. This is used to mask LE events that we do not know how to handle.
  uint64_t BuildLEEventMask();

  // Called by ShutDown() and during Initialize() in case of failure. This synchronously cleans up
  // the transports and resets initialization state.
  void CleanUp();

  // Called by Transport after it has been unexpectedly closed.
  void OnTransportClosed();

  // Uniquely identifies this adapter on the current system.
  std::string identifier_;

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  ftl::RefPtr<hci::Transport> hci_;

  // Callback invoked to notify clients when the underlying transport is closed.
  ftl::Closure transport_closed_cb_;

  // Parameters relevant to the initialization sequence.
  // TODO(armansito): The Initialize()/ShutDown() pattern has become common enough in this project
  // that it might be worth considering moving the init-state-keeping into an abstract base.
  enum State {
    kNotInitialized = 0,
    kInitializing,
    kInitialized,
  };
  std::atomic<State> init_state_;
  std::unique_ptr<hci::SequentialCommandRunner> init_seq_runner_;

  // Contains the global adapter state.
  AdapterState state_;

  // This must remain the last member to make sure that all weak pointers are invalidating before
  // other members are destroyed.
  ftl::WeakPtrFactory<Adapter> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Adapter);
};

}  // namespace gap
}  // namespace bluetooth
