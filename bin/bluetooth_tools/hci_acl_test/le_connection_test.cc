// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "le_connection_test.h"

#include <endian.h>

#include "apps/bluetooth/lib/hci/command_packet.h"
#include "apps/bluetooth/lib/hci/defaults.h"
#include "apps/bluetooth/lib/hci/device_wrapper.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

using std::placeholders::_1;
using std::placeholders::_2;

using namespace bluetooth;

namespace hci_acl_test {

LEConnectionTest::LEConnectionTest() : le_conn_complete_handler_id_(0u), disconn_handler_id_(0u) {}

bool LEConnectionTest::Run(ftl::UniqueFD hci_dev_fd, const common::DeviceAddressBytes& dst_addr) {
  FTL_DCHECK(hci_dev_fd.is_valid());

  auto hci_dev = std::make_unique<hci::MagentaDeviceWrapper>(std::move(hci_dev_fd));
  hci_ = hci::Transport::Create(std::move(hci_dev));
  if (!hci_->Initialize()) {
    FTL_LOG(ERROR) << "Failed to initialize HCI transport";
    return false;
  }

  dst_addr_ = dst_addr;

  // We can pass this by reference to the callbacks below since the MessageLoop is run within this
  // scope and hence these variables will remain in scope.
  hci::DataBufferInfo bredr_buffer_info;

  auto read_buf_size_cb = [&](hci::CommandChannel::TransactionId id,
                              const hci::EventPacket& reply) {
    auto return_params = reply.GetReturnParams<hci::ReadBufferSizeReturnParams>();
    if (return_params->status != hci::Status::kSuccess) {
      LogErrorStatusAndQuit("Read Buffer Size failed", return_params->status);
      return;
    }

    bredr_buffer_info = hci::DataBufferInfo(le16toh(return_params->hc_acl_data_packet_length),
                                            le16toh(return_params->hc_total_num_acl_data_packets));
  };
  auto le_read_buf_size_cb = [&, this](hci::CommandChannel::TransactionId id,
                                       const hci::EventPacket& reply) {
    auto return_params = reply.GetReturnParams<hci::LEReadBufferSizeReturnParams>();
    if (return_params->status != hci::Status::kSuccess) {
      LogErrorStatusAndQuit("LE Read Buffer Size failed", return_params->status);
      return;
    }

    hci::DataBufferInfo le_buffer_info(le16toh(return_params->hc_le_acl_data_packet_length),
                                       le16toh(return_params->hc_total_num_le_acl_data_packets));

    InitializeDataChannelAndCreateConnection(bredr_buffer_info, le_buffer_info);
  };

  common::StaticByteBuffer<hci::CommandPacket::GetMinBufferSize(0)> buffer;

  // Read Buffer Size
  hci::CommandPacket cmd(hci::kReadBufferSize, &buffer, 0);
  cmd.EncodeHeader();
  hci_->command_channel()->SendCommand(common::DynamicByteBuffer(buffer),
                                       GetStatusCallback("Read Buffer Size"), read_buf_size_cb,
                                       message_loop_.task_runner());

  // LE Read Buffer Size
  cmd = hci::CommandPacket(hci::kLEReadBufferSize, &buffer, 0);
  cmd.EncodeHeader();
  hci_->command_channel()->SendCommand(common::DynamicByteBuffer(buffer),
                                       GetStatusCallback("LE Read Buffer Size"),
                                       le_read_buf_size_cb, message_loop_.task_runner());

  message_loop_.Run();

  return true;
}

void LEConnectionTest::InitializeDataChannelAndCreateConnection(
    const bluetooth::hci::DataBufferInfo& bredr_buffer_info,
    const bluetooth::hci::DataBufferInfo& le_buffer_info) {
  auto conn_lookup_cb = [this](hci::ConnectionHandle handle) -> ftl::RefPtr<hci::Connection> {
    auto iter = conn_map_.find(handle);
    if (iter == conn_map_.end()) return nullptr;
    return iter->second;
  };
  if (!hci_->InitializeACLDataChannel(bredr_buffer_info, le_buffer_info, conn_lookup_cb)) {
    FTL_LOG(ERROR) << "Failed to initialize ACL data channel";
    message_loop_.QuitNow();
    return;
  }
  hci_->acl_data_channel()->SetDataRxHandler(
      std::bind(&LEConnectionTest::ACLDataRxCallback, this, _1), message_loop_.task_runner());

  // Connection parameters with reasonable defaults.
  hci::LEConnectionParams conn_params(hci::LEPeerAddressType::kPublic, dst_addr_);

  // LE Create Connection.
  constexpr size_t kPayloadSize = sizeof(hci::LECreateConnectionCommandParams);
  common::StaticByteBuffer<hci::CommandPacket::GetMinBufferSize(kPayloadSize)> buffer;
  hci::CommandPacket cmd(hci::kLECreateConnection, &buffer, kPayloadSize);

  auto params = cmd.mutable_payload<hci::LECreateConnectionCommandParams>();
  params->scan_interval = htole16(hci::defaults::kLEScanInterval);
  params->scan_window = htole16(hci::defaults::kLEScanWindow);
  params->initiator_filter_policy = hci::GenericEnableParam::kDisable;
  params->peer_address_type = hci::LEAddressType::kPublic;
  params->peer_address = conn_params.peer_address();
  params->own_address_type = hci::LEOwnAddressType::kPublic;
  params->conn_interval_min = htole16(conn_params.connection_interval_min());
  params->conn_interval_max = htole16(conn_params.connection_interval_max());
  params->conn_latency = htole16(conn_params.connection_latency());
  params->supervision_timeout = htole16(conn_params.supervision_timeout());
  params->minimum_ce_length = 0x0000;
  params->maximum_ce_length = 0x0000;

  cmd.EncodeHeader();

  // Since this is a background task, we use HCI_Command_Status as the completion callback.
  auto le_conn_status_cb = [this](hci::CommandChannel::TransactionId id,
                                  const hci::EventPacket& event) {
    FTL_DCHECK(event.event_code() == hci::kCommandStatusEventCode);

    const auto& payload = event.payload<hci::CommandStatusEventParams>();
    FTL_DCHECK(le16toh(payload.command_opcode) == hci::kLECreateConnection);

    if (payload.status != hci::Status::kSuccess) {
      LogErrorStatusAndQuit("LE Create Connection Status (failed)", payload.status);
    }
  };

  // This is the event that signals the completion of a connection.
  auto le_conn_complete_cb = [ this, orig_params = conn_params ](const hci::EventPacket& event) {
    FTL_DCHECK(event.event_code() == hci::kLEMetaEventCode);
    FTL_DCHECK(event.payload<hci::LEMetaEventParams>().subevent_code ==
               hci::kLEConnectionCompleteSubeventCode);

    auto params = event.GetLEEventParams<hci::LEConnectionCompleteSubeventParams>();
    if (params->status != hci::Status::kSuccess) {
      LogErrorStatusAndQuit("LE Connection Complete (failed)", params->status);
      return;
    }

    hci::LEConnectionParams conn_params(
        params->peer_address_type, params->peer_address, orig_params.connection_interval_min(),
        orig_params.connection_interval_max(), le16toh(params->conn_interval),
        le16toh(params->conn_latency), le16toh(params->supervision_timeout));

    auto conn = hci::Connection::NewLEConnection(le16toh(params->connection_handle),
                                                 (params->role == hci::LEConnectionRole::kMaster)
                                                     ? hci::Connection::Role::kMaster
                                                     : hci::Connection::Role::kSlave,
                                                 conn_params);

    FTL_LOG(INFO) << "LE Connection Complete - handle: "
                  << ftl::StringPrintf("0x%04x", conn->handle())
                  << ", BD_ADDR: " << conn_params.peer_address().ToString();

    conn_map_[conn->handle()] = conn;

    // We're done with this event. Unregister the handler.
    hci_->command_channel()->RemoveEventHandler(le_conn_complete_handler_id_);
    le_conn_complete_handler_id_ = 0u;

    // Register a disconnect handler.
    auto disconn_cb = [this](const hci::EventPacket& event) {
      FTL_DCHECK(event.event_code() == hci::kDisconnectionCompleteEventCode);

      const auto& params = event.payload<hci::DisconnectionCompleteEventParams>();
      auto iter = conn_map_.find(le16toh(params.connection_handle));
      if (iter == conn_map_.end()) {
        FTL_LOG(ERROR) << "Received Disconnection Complete event for unknown handle";
        return;
      }

      conn_map_.erase(iter);
      FTL_LOG(INFO) << ftl::StringPrintf("Disconnected - reason: 0x%02x", params.reason);
      hci_->command_channel()->RemoveEventHandler(disconn_handler_id_);
      message_loop_.QuitNow();
    };

    disconn_handler_id_ = hci_->command_channel()->AddEventHandler(
        hci::kDisconnectionCompleteEventCode, disconn_cb, message_loop_.task_runner());

    SendNotifications();
  };

  le_conn_complete_handler_id_ = hci_->command_channel()->AddLEMetaEventHandler(
      hci::kLEConnectionCompleteSubeventCode, le_conn_complete_cb, message_loop_.task_runner());

  FTL_LOG(INFO) << "Sending LE connection request";

  // The status callback will never get called but we pass one in anyway.
  hci_->command_channel()->SendCommand(common::DynamicByteBuffer(buffer),
                                       GetStatusCallback("LE Create Connection"), le_conn_status_cb,
                                       message_loop_.task_runner(), hci::kCommandStatusEventCode);
}

void LEConnectionTest::SendNotifications() {
  // Just send back an error response:
  //    - 4-octet L2CAP header.
  //    - 4-octet ATT protocol Handle-Value Notification.
  constexpr size_t kDataLength = 8;
  for (unsigned int i = 0; i < hci_->acl_data_channel()->GetLEBufferInfo().max_num_packets() * 3;
       ++i) {
    common::DynamicByteBuffer rsp_bytes(hci::ACLDataTxPacket::GetMinBufferSize(kDataLength));
    hci::ACLDataTxPacket rsp(conn_map_.begin()->first,
                             hci::ACLPacketBoundaryFlag::kFirstNonFlushable,
                             hci::ACLBroadcastFlag::kPointToPoint, kDataLength, &rsp_bytes);
    auto payload = rsp.mutable_payload_data();
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

    rsp.EncodeHeader();

    hci_->acl_data_channel()->SendPacket(std::move(rsp_bytes));
  }
}

void LEConnectionTest::ACLDataRxCallback(common::DynamicByteBuffer rx_bytes) {
  hci::ACLDataRxPacket packet(&rx_bytes);
  FTL_LOG(INFO) << "Received ACL packet on handle: "
                << ftl::StringPrintf("0x%04x", packet.GetConnectionHandle());

  // Since this is an LE connection using a ACL-U logical link the payload should contain a L2CAP
  // packet. Look at the channel ID, if this is a ATT protocol request then send back an error
  // response, otherwise just sit back and let the connection time out.

  // The L2CAP header contains 4 bytes: 2-octet length and 2-octet channel ID.
  auto rx_payload = packet.payload_data();
  if (rx_payload.size() < 5) return;

  uint16_t l2cap_channel_id = le16toh(*reinterpret_cast<const uint16_t*>(rx_payload.data() + 2));
  if (l2cap_channel_id != 4) return;

  FTL_LOG(INFO) << "Got L2CAP frame on ATT protocol channel!";

  // Just send back an error response:
  //    - 4-octet L2CAP header.
  //    - 5-octet ATT Error Response.
  constexpr size_t kDataLength = 9;
  common::DynamicByteBuffer rsp_bytes(hci::ACLDataTxPacket::GetMinBufferSize(kDataLength));
  hci::ACLDataTxPacket rsp(packet.GetConnectionHandle(),
                           hci::ACLPacketBoundaryFlag::kFirstNonFlushable,
                           hci::ACLBroadcastFlag::kPointToPoint, kDataLength, &rsp_bytes);
  auto rsp_payload = rsp.mutable_payload_data();
  // L2CAP: payload length
  rsp_payload[0] = 0x05;
  rsp_payload[1] = 0x00;

  // L2CAP: ATT channel ID
  rsp_payload[2] = 0x04;
  rsp_payload[3] = 0x00;

  // ATT: Opcode: Error Response
  rsp_payload[4] = 0x01;

  // ATT: Request Opcode (from original packet)
  rsp_payload[5] = rx_payload[4];

  // ATT: Attribute Handle
  rsp_payload[6] = 0x00;
  rsp_payload[7] = 0x00;

  // ATT: Error Code: Request Not Supported
  rsp_payload[8] = 0x06;

  rsp.EncodeHeader();
  hci_->acl_data_channel()->SendPacket(std::move(rsp_bytes));
}

void LEConnectionTest::LogErrorStatusAndQuit(const std::string& msg, hci::Status status) {
  FTL_LOG(ERROR) << ftl::StringPrintf("%s: 0x%02x", msg.c_str(), status);
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
