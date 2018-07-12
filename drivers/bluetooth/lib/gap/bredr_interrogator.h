// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/common/optional.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"
#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/connection_parameters.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "lib/fxl/functional/cancelable_callback.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace btlib {
namespace hci {
class Transport;
}

namespace gap {

// A BrEdrInterrogator abstracts over the HCI commands and events involved
// immediately after connecting to a remote device over BR/EDR.
// It also provides a way to hold pending connections while waiting for
// confirmation and times out those connections out when we do not get a
// response.
//
// This class owns a Connection object while interrogation happens.
//
// Only one interregator object is expected to exist per controller.
class BrEdrInterrogator {
 public:
  // |cache| must live longer than this object.
  BrEdrInterrogator(RemoteDeviceCache* cache, fxl::RefPtr<hci::Transport> hci,
                    async_dispatcher_t* dispatcher);

  // Will cancel all uncompleted interrogations.
  ~BrEdrInterrogator();

  // Starts interrogation. Calls |callback| when the sequence is completed or
  // abandoned.
  using ResultCallback =
      std::function<void(hci::Status status, hci::ConnectionPtr conn_ptr)>;
  void Start(const std::string& device_id, hci::ConnectionPtr conn_ptr,
             ResultCallback callback);

  // Abandons any interrogation of |device_id|.  Their callbacks will be called
  // with a Status of Canceled.
  void Cancel(std::string device_id);

 private:
  // Completes |device| if there is nothing else to ask.
  void MaybeComplete(const std::string& device_id);

  // Completes interrogation on |device| with |status|, possibly early.
  void Complete(std::string device_id, hci::Status status);

  // Reade the remote version information from the device.
  void ReadRemoteVersionInformation(const std::string& device_id,
                                    hci::ConnectionHandle handle);

  // Requests the name of the remote device.
  void MakeRemoteNameRequest(const std::string& device_id);

  // Requests features of |device|, and asks for Extended Features if they
  // exist.
  void ReadRemoteFeatures(const std::string& device_id,
                          hci::ConnectionHandle handle);

  // Reads the extended feature page |page| of |device|.
  void ReadRemoteExtendedFeatures(const std::string& device_id,
                                  hci::ConnectionHandle handle, uint8_t page);

  using CancelableCommandCallback = fxl::CancelableCallback<void(
      hci::CommandChannel::TransactionId id, const hci::EventPacket& event)>;
  struct Interrogation {
    Interrogation(hci::ConnectionPtr conn_ptr, ResultCallback callback);
    ~Interrogation();
    FXL_DISALLOW_COPY_AND_ASSIGN(Interrogation);

    // Connection to |device|
    hci::ConnectionPtr conn_ptr;

    // Callback for results.
    ResultCallback result_cb;

    // Set of callbacks we cancel if we stop the interrogation.
    std::deque<CancelableCommandCallback> callbacks;

    // Finishes the interrogation, calling the callback.
    void Finish(hci::Status status);
  };

  // The hci transport to use.
  fxl::RefPtr<hci::Transport> hci_;

  // The dispatcher we use.
  async_dispatcher_t* dispatcher_;

  // Cache to retrieve devices from.
  RemoteDeviceCache* cache_;

  // The current set of interrogations
  std::unordered_map<std::string, std::unique_ptr<Interrogation>> pending_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<BrEdrInterrogator> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BrEdrInterrogator);
};

}  // namespace gap
}  // namespace btlib
