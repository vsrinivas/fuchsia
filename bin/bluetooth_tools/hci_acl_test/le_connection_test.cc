// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "le_connection_test.h"

#include <endian.h>

#include "apps/bluetooth/lib/hci/control_packets.h"
#include "apps/bluetooth/lib/hci/defaults.h"
#include "apps/bluetooth/lib/hci/device_wrapper.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

using std::placeholders::_1;
using std::placeholders::_2;

using namespace bluetooth;

namespace hci_acl_test {

LEConnectionTest::LEConnectionTest() : le_conn_complete_handler_id_(0u), disconn_handler_id_(0u) {}

bool LEConnectionTest::Run(fxl::UniqueFD hci_dev_fd, const common::DeviceAddress& dst_addr,
                           bool cancel_right_away) {
  FXL_DCHECK(hci_dev_fd.is_valid());

  auto hci_dev = std::make_unique<hci::MagentaDeviceWrapper>(std::move(hci_dev_fd));
  hci_ = hci::Transport::Create(std::move(hci_dev));
  if (!hci_->Initialize()) {
    FXL_LOG(ERROR) << "Failed to initialize HCI transport";
    return false;
  }

  dst_addr_ = dst_addr;

  // We can pass this by reference to the callbacks below since the MessageLoop is run within this
  // scope and hence these variables will remain in scope.
  hci::DataBufferInfo bredr_buffer_info;

  auto read_buf_size_cb = [&](hci::CommandChannel::TransactionId id,
                              const hci::EventPacket& reply) {
    auto return_params = reply.return_params<hci::ReadBufferSizeReturnParams>();
    if (return_params->status != hci::Status::kSuccess) {
      LogErrorStatusAndQuit("Read Buffer Size failed", return_params->status);
      return;
    }

    bredr_buffer_info = hci::DataBufferInfo(le16toh(return_params->hc_acl_data_packet_length),
                                            le16toh(return_params->hc_total_num_acl_data_packets));
  };
  auto le_read_buf_size_cb = [&, this](hci::CommandChannel::TransactionId id,
                                       const hci::EventPacket& reply) {
    auto return_params = reply.return_params<hci::LEReadBufferSizeReturnParams>();
    if (return_params->status != hci::Status::kSuccess) {
      LogErrorStatusAndQuit("LE Read Buffer Size failed", return_params->status);
      return;
    }

    hci::DataBufferInfo le_buffer_info(le16toh(return_params->hc_le_acl_data_packet_length),
                                       le16toh(return_params->hc_total_num_le_acl_data_packets));

    InitializeDataChannelAndCreateConnection(bredr_buffer_info, le_buffer_info, cancel_right_away);
  };

  // Read Buffer Size
  hci_->command_channel()->SendCommand(hci::CommandPacket::New(hci::kReadBufferSize),
                                       message_loop_.task_runner(), read_buf_size_cb,
                                       GetStatusCallback("Read Buffer Size"));

  // LE Read Buffer Size
  hci_->command_channel()->SendCommand(hci::CommandPacket::New(hci::kLEReadBufferSize),
                                       message_loop_.task_runner(), le_read_buf_size_cb,
                                       GetStatusCallback("LE Read Buffer Size"));

  message_loop_.Run();

  return true;
}

void LEConnectionTest::InitializeDataChannelAndCreateConnection(
    const bluetooth::hci::DataBufferInfo& bredr_buffer_info,
    const bluetooth::hci::DataBufferInfo& le_buffer_info, bool cancel_right_away) {
  if (!hci_->InitializeACLDataChannel(bredr_buffer_info, le_buffer_info)) {
    FXL_LOG(ERROR) << "Failed to initialize ACL data channel";
    message_loop_.QuitNow();
    return;
  }
  hci_->acl_data_channel()->SetDataRxHandler(
      std::bind(&LEConnectionTest::ACLDataRxCallback, this, _1));

  // Connection parameters with reasonable defaults.
  hci::Connection::LowEnergyParameters conn_params(hci::defaults::kLEConnectionIntervalMin,
                                                   hci::defaults::kLEConnectionIntervalMax, 0x0000,
                                                   0x0000, hci::defaults::kLESupervisionTimeout);

  // LE Create Connection.
  constexpr size_t kPayloadSize = sizeof(hci::LECreateConnectionCommandParams);
  auto cmd = hci::CommandPacket::New(hci::kLECreateConnection, kPayloadSize);

  auto params = cmd->mutable_view()->mutable_payload<hci::LECreateConnectionCommandParams>();
  params->scan_interval = htole16(hci::defaults::kLEScanInterval);
  params->scan_window = htole16(hci::defaults::kLEScanWindow);
  params->initiator_filter_policy = hci::GenericEnableParam::kDisable;
  params->peer_address_type = (dst_addr_.type() == common::DeviceAddress::Type::kLEPublic)
                                  ? hci::LEAddressType::kPublic
                                  : hci::LEAddressType::kRandom;
  params->peer_address = dst_addr_.value();
  params->own_address_type = hci::LEOwnAddressType::kPublic;
  params->conn_interval_min = htole16(conn_params.interval_min());
  params->conn_interval_max = htole16(conn_params.interval_max());
  params->conn_latency = htole16(conn_params.latency());
  params->supervision_timeout = htole16(conn_params.supervision_timeout());
  params->minimum_ce_length = 0x0000;
  params->maximum_ce_length = 0x0000;

  // Since this is a background task, we use HCI_Command_Status as the completion callback.
  auto le_conn_status_cb = [this](hci::CommandChannel::TransactionId id,
                                  const hci::EventPacket& event) {
    FXL_DCHECK(event.event_code() == hci::kCommandStatusEventCode);

    const auto& payload = event.view().payload<hci::CommandStatusEventParams>();
    FXL_DCHECK(le16toh(payload.command_opcode) == hci::kLECreateConnection);

    if (payload.status != hci::Status::kSuccess) {
      LogErrorStatusAndQuit("LE Create Connection Status (failed)", payload.status);
    }
  };

  // This is the event that signals the completion of a connection.
  auto le_conn_complete_cb = [this](const hci::EventPacket& event) {
    FXL_DCHECK(event.event_code() == hci::kLEMetaEventCode);
    FXL_DCHECK(event.view().payload<hci::LEMetaEventParams>().subevent_code ==
               hci::kLEConnectionCompleteSubeventCode);

    auto params = event.le_event_params<hci::LEConnectionCompleteSubeventParams>();
    if (params->status != hci::Status::kSuccess) {
      LogErrorStatusAndQuit("LE Connection Complete (failed)", params->status);
      return;
    }

    FXL_LOG(INFO) << "LE Connection Complete - handle: "
                  << fxl::StringPrintf("0x%04x", le16toh(params->connection_handle))
                  << ", BD_ADDR: " << dst_addr_.value().ToString();

    // We're done with this event. Unregister the handler.
    hci_->command_channel()->RemoveEventHandler(le_conn_complete_handler_id_);
    le_conn_complete_handler_id_ = 0u;

    // Register a disconnect handler.
    auto disconn_cb = [this](const hci::EventPacket& event) {
      FXL_DCHECK(event.event_code() == hci::kDisconnectionCompleteEventCode);

      const auto& params = event.view().payload<hci::DisconnectionCompleteEventParams>();

      FXL_LOG(INFO) << fxl::StringPrintf("Disconnected - handle: 0x%02x, reason: 0x%02x",
                                         le16toh(params.connection_handle), params.reason);
      hci_->command_channel()->RemoveEventHandler(disconn_handler_id_);
      message_loop_.QuitNow();
    };

    disconn_handler_id_ = hci_->command_channel()->AddEventHandler(
        hci::kDisconnectionCompleteEventCode, disconn_cb, message_loop_.task_runner());

    SendNotifications(le16toh(params->connection_handle));
  };

  le_conn_complete_handler_id_ = hci_->command_channel()->AddLEMetaEventHandler(
      hci::kLEConnectionCompleteSubeventCode, le_conn_complete_cb, message_loop_.task_runner());

  FXL_LOG(INFO) << "Sending LE connection request";

  // The status callback will never get called but we pass one in anyway.
  hci_->command_channel()->SendCommand(std::move(cmd), message_loop_.task_runner(),
                                       le_conn_status_cb, nullptr, hci::kCommandStatusEventCode);

  if (cancel_right_away) {
    auto cancel = hci::CommandPacket::New(hci::kLECreateConnectionCancel);
    auto cancel_complete_cb = [this](auto id, const hci::EventPacket& event) {
      auto status = event.return_params<hci::SimpleReturnParams>()->status;
      if (status != hci::Status::kSuccess) {
        LogErrorStatusAndQuit("LE Create Connection Cancel (failed)", status);
      }
    };
    hci_->command_channel()->SendCommand(std::move(cancel), message_loop_.task_runner(),
                                         cancel_complete_cb,
                                         GetStatusCallback("LE Create Connection Cancel"));
  }
}

void LEConnectionTest::SendNotifications(hci::ConnectionHandle connection_handle) {
  // Just send back an error response:
  //    - 4-octet L2CAP header.
  //    - 4-octet ATT protocol Handle-Value Notification.
  constexpr size_t kDataLength = 8;
  for (unsigned int i = 0; i < hci_->acl_data_channel()->GetLEBufferInfo().max_num_packets() * 3;
       ++i) {
    auto packet =
        hci::ACLDataPacket::New(connection_handle, hci::ACLPacketBoundaryFlag::kFirstNonFlushable,
                                hci::ACLBroadcastFlag::kPointToPoint, kDataLength);
    auto payload = packet->mutable_view()->mutable_payload_data();

    // L2CAP: payload length
    payload[0] = 0x04;
    payload[1] = 0x00;

    // L2CAP: ATT channel ID
    payload[2] = 0x04;
    payload[3] = 0x00;

    // ATT: Opcode: Error Response
    payload[4] = 0x1B;

    // ATT: Attribute Handle (0x0003, because why not)
    payload[5] = 0x03;
    payload[6] = 0x00;

    // ATT: Attribute Value
    payload[7] = 0x00;

    hci_->acl_data_channel()->SendPacket(std::move(packet), hci::Connection::LinkType::kLE);
  }
}

void LEConnectionTest::ACLDataRxCallback(std::unique_ptr<hci::ACLDataPacket> packet) {
  FXL_LOG(INFO) << "Received ACL packet on handle: "
                << fxl::StringPrintf("0x%04x", packet->connection_handle());

  // Since this is an LE connection using a LE-U logical link the payload should contain a L2CAP
  // packet. Look at the channel ID, if this is a ATT protocol request then send back an error
  // response, otherwise just sit back and let the connection time out.

  // The L2CAP header contains 4 bytes: 2-octet length and 2-octet channel ID.
  const auto payload = packet->view().payload_data();

  if (payload.size() < 5) return;

  uint16_t l2cap_channel_id = le16toh(*reinterpret_cast<const uint16_t*>(payload.data() + 2));
  if (l2cap_channel_id != 4) return;

  FXL_LOG(INFO) << "Got L2CAP frame on ATT protocol channel!";

  // Just send back an error response:
  //    - 4-octet L2CAP header.
  //    - 5-octet ATT Error Response.
  constexpr size_t kDataLength = 9;
  auto response = hci::ACLDataPacket::New(packet->connection_handle(),
                                          hci::ACLPacketBoundaryFlag::kFirstNonFlushable,
                                          hci::ACLBroadcastFlag::kPointToPoint, kDataLength);
  auto rsp_payload = response->mutable_view()->mutable_payload_bytes();

  // L2CAP: payload length
  rsp_payload[0] = 0x05;
  rsp_payload[1] = 0x00;

  // L2CAP: ATT channel ID
  rsp_payload[2] = 0x04;
  rsp_payload[3] = 0x00;

  // ATT: Opcode: Error Response
  rsp_payload[4] = 0x01;

  // ATT: Request Opcode (from original packet)
  rsp_payload[5] = payload[4];

  // ATT: Attribute Handle
  rsp_payload[6] = 0x00;
  rsp_payload[7] = 0x00;

  // ATT: Error Code: Request Not Supported
  rsp_payload[8] = 0x06;

  hci_->acl_data_channel()->SendPacket(std::move(response), hci::Connection::LinkType::kLE);
}

void LEConnectionTest::LogErrorStatusAndQuit(const std::string& msg, hci::Status status) {
  FXL_LOG(ERROR) << fxl::StringPrintf("%s: 0x%02x", msg.c_str(), status);
  message_loop_.QuitNow();
}

hci::CommandChannel::CommandStatusCallback LEConnectionTest::GetStatusCallback(
    const std::string& command_name) {
  return std::bind(&LEConnectionTest::StatusCallback, this, command_name, _1, _2);
}

void LEConnectionTest::StatusCallback(const std::string& command_name,
                                      bluetooth::hci::CommandChannel::TransactionId id,
                                      bluetooth::hci::Status status) {
  LogErrorStatusAndQuit(command_name + " Command Status", status);
}

}  // namespace hci_acl_test
