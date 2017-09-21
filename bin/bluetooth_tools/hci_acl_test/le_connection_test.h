// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/acl_data_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fsl/tasks/message_loop.h"

namespace hci_acl_test {

// This is a LE connection tester that works directly against the HCI transport classes. This tester
// performs the following:
//
//   - Initialize HCI transport.
//   - Obtain buffer size information from the controller.
//   - Create direct LE connection to a remote device with a public BD_ADDR.
//   - Listen to ACL packets and respond the ATT protocol requests without any L2CAP state
//     management.
class LEConnectionTest final {
 public:
  LEConnectionTest();
  bool Run(fxl::UniqueFD hci_dev_fd, const bluetooth::common::DeviceAddress& dst_addr,
           bool cancel_right_away = false);

 private:
  // Initializes the data channel and sends a LE connection request to |dst_addr_|. Exits the
  // run loop if an error occurs.
  void InitializeDataChannelAndCreateConnection(
      const bluetooth::hci::DataBufferInfo& bredr_buffer_info,
      const bluetooth::hci::DataBufferInfo& le_buffer_info, bool cancel_right_away);

  // Called after the connection has been successfully established. Sends 3 times the maximum number
  // of LE packets that can be stored in the controller's buffers. Sends ATT protocol Handle-Value
  // notification PDUs.
  void SendNotifications(bluetooth::hci::ConnectionHandle connection_handle);

  // Called when ACL data packets are received.
  void ACLDataRxCallback(std::unique_ptr<bluetooth::hci::ACLDataPacket> packet);

  // Logs the given message and status code and exits the run loop.
  void LogErrorStatusAndQuit(const std::string& msg, bluetooth::hci::Status status);

  // Returns a status callback that can be used while sending commands.
  bluetooth::hci::CommandChannel::CommandStatusCallback GetStatusCallback(
      const std::string& command_name);

  // Logs the status code and exits the run loop.
  void StatusCallback(const std::string& command_name,
                      bluetooth::hci::CommandChannel::TransactionId id,
                      bluetooth::hci::Status status);

  fxl::RefPtr<bluetooth::hci::Transport> hci_;
  fsl::MessageLoop message_loop_;
  bluetooth::common::DeviceAddress dst_addr_;
  bluetooth::hci::CommandChannel::EventHandlerId le_conn_complete_handler_id_;
  bluetooth::hci::CommandChannel::EventHandlerId disconn_handler_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LEConnectionTest);
};

}  // namespace hci_acl_test
