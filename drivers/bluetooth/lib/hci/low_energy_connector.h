// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/common/optional.h"
#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "lib/fxl/functional/cancelable_callback.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace bluetooth {
namespace hci {

class Transport;

// A LowEnergyConnector abstracts over the HCI commands and events involved in
// initiating a direct link-layer connection with a peer device. This class also
// provides a way for a delegate to be notified when a connection is initiated
// by a remote.
//
// This class vends Connection objects for LE link layer connections.
//
// Instances of this class are expected to each exist as a singleton on a
// per-transport basis as multiple instances cannot accurately reflect the state
// of the controller while allowing simultaneous operations.
class LowEnergyConnector {
 public:
  // The constructor expects the following arguments:
  //   - |hci|: The HCI transport this should operate on.
  //
  //   - |task_runner|: The task runner that will be used to run all
  //     asynchronous operations. This must be bound to the thread on which the
  //     LowEnergyConnector is created.
  //
  //   - |delegate|: The delegate that will be notified when a new logical link
  //     is established. This can be due to either an explicit request via
  //     CreateConnection() (local host initiated), an incoming request (remote
  //     initiated), or autonomously created using the controller white list
  //     (local controller initiated).
  using ConnectionDelegate =
      std::function<void(std::unique_ptr<Connection> connection)>;
  LowEnergyConnector(fxl::RefPtr<Transport> hci,
                     fxl::RefPtr<fxl::TaskRunner> task_runner,
                     const ConnectionDelegate& delegate);

  // Deleting an instance cancels any pending connection request.
  ~LowEnergyConnector();

  // Creates a LE link layer connection to the remote device identified by
  // |peer_address| with initial connection parameters |initial_parameters|.
  // Returns false, if a create connection request is currently pending.
  //
  // |own_address_type| indicates which local Bluetooth address will be used
  // during the request.
  //
  // If |use_whitelist| is true, then the controller white list is used to
  // determine which advertiser to connect to. Otherwise, the controller will
  // connect to |peer_address|.
  //
  // |result_callback| is called asynchronously to notify the result of the
  // operation. On success, the delegate will be notified with a new connection
  // object.
  //
  // |timeout_ms| specifies a time period after which the request will time out.
  // When a request to create connection times out, |result_callback| will be
  // called with its |result| parameter set to Result::kFailed and |hci_status|
  // set to Status::kCommandTimeout.
  enum class Result {
    kSuccess,
    kCanceled,
    kFailed,
  };
  using ResultCallback = std::function<void(Result result, Status hci_status)>;
  bool CreateConnection(
      LEOwnAddressType own_address_type,
      bool use_whitelist,
      const common::DeviceAddress& peer_address,
      uint16_t scan_interval,
      uint16_t scan_window,
      const Connection::LowEnergyParameters& initial_parameters,
      const ResultCallback& result_callback,
      int64_t timeout_ms);

  // Cancels the currently pending connection attempt.
  void Cancel();

  // Returns true if a connection request is currently pending.
  bool request_pending() const { return pending_request_.HasValue(); }

 private:
  struct PendingRequest {
    PendingRequest() = default;
    PendingRequest(const common::DeviceAddress& peer_address,
                   uint16_t interval_min,
                   uint16_t interval_max,
                   const ResultCallback& result_callback);

    bool canceled;
    bool timed_out;
    common::DeviceAddress peer_address;
    uint16_t interval_min;
    uint16_t interval_max;
    ResultCallback result_callback;
  };

  // Called by Cancel() and by OnCreateConnectionTimeout().
  void CancelInternal(bool timed_out = false);

  // Event handler for the HCI LE Connection Complete event.
  void OnConnectionCompleteEvent(const EventPacket& event);

  // Called when a LE Create Connection request has completed.
  void OnCreateConnectionComplete(Result result, Status hci_status);

  // Called when a LE Create Connection request has timed out.
  void OnCreateConnectionTimeout();

  // Task runner for all asynchronous tasks.
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  // The HCI transport.
  fxl::RefPtr<Transport> hci_;

  // The delegate that gets notified when a new link layer connection gets
  // created.
  ConnectionDelegate delegate_;

  // The currently pending request.
  common::Optional<PendingRequest> pending_request_;

  // Callback that is invoked when a request to create connection times out. We
  // do not rely on CommandChannel's timer since the request completes when we
  // receive the HCI Command Status event.
  fxl::CancelableClosure request_timeout_cb_;

  // Our event handle ID for the LE Connection Complete event.
  CommandChannel::EventHandlerId event_handler_id_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<LowEnergyConnector> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnector);
};

}  // namespace hci
}  // namespace bluetooth
