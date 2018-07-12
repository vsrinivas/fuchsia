// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/lib/gap/bredr_interrogator.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"

namespace btlib {

namespace hci {
class SequentialCommandRunner;
class Transport;
}  // namespace hci

namespace gap {

class RemoteDeviceCache;

// Manages all activity related to connections in the BR/EDR section of the
// controller, including whether the device can be connected to, incoming
// connections, and initiating connections.
class BrEdrConnectionManager {
 public:
  BrEdrConnectionManager(fxl::RefPtr<hci::Transport> hci,
                         RemoteDeviceCache* device_cache,
                         bool use_interlaced_scan);
  ~BrEdrConnectionManager();

  // Set whether this host is connectable
  void SetConnectable(bool connectable, hci::StatusCallback status_cb);

 private:
  // Reads the controller page scan settings.
  void ReadPageScanSettings();

  // Writes page scan parameters to the controller.
  // If |interlaced| is true, and the controller does not support interlaced
  // page scan mode, standard mode is used.
  void WritePageScanSettings(uint16_t interval,
                             uint16_t window,
                             bool interlaced,
                             hci::StatusCallback cb);

  // Called when a ConnectionRequest event is received.
  void OnConnectionRequest(const hci::EventPacket& event);

  // Called when a ConnectionComplete event is received.
  void OnConnectionComplete(const hci::EventPacket& event);

  // Called when a DisconnectComplete event is received.
  void OnDisconnectionComplete(const hci::EventPacket& event);

  fxl::RefPtr<hci::Transport> hci_;
  std::unique_ptr<hci::SequentialCommandRunner> hci_cmd_runner_;

  // Device cache is used to look up parameters for connecting to devices and
  // update the state of connected devices as well as introduce unknown devices.
  // This object must outlive this instance.
  // TODO(NET-410) - put newly found devices OnConnectionRequest/Complete
  // and use for Connect()
  RemoteDeviceCache* cache_ __UNUSED;

  // Interregator for new connections to pass.
  BrEdrInterrogator interrogator_;

  // Connections that are active.
  std::unordered_map<std::string, hci::ConnectionPtr> connections_;

  // Handler ID for connection events
  hci::CommandChannel::EventHandlerId conn_complete_handler_id_;
  hci::CommandChannel::EventHandlerId conn_request_handler_id_;
  hci::CommandChannel::EventHandlerId disconn_cmpl_handler_id_;

  // The current page scan parameters of the controller.
  // Set to 0 when non-connectable.
  uint16_t page_scan_interval_;
  uint16_t page_scan_window_;
  hci::PageScanType page_scan_type_;
  bool use_interlaced_scan_;

  // The dispatcher that all commands are queued on.
  async_dispatcher_t* dispatcher_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<BrEdrConnectionManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BrEdrConnectionManager);
};

}  // namespace gap
}  // namespace btlib
