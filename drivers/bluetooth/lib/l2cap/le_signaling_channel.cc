// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "le_signaling_channel.h"

#include "lib/fxl/strings/string_printf.h"

#include "channel.h"
#include "logical_link.h"

namespace bluetooth {
namespace l2cap {
namespace internal {

LESignalingChannel::LESignalingChannel(std::unique_ptr<Channel> chan,
                                       hci::Connection::Role role)
    : SignalingChannel(std::move(chan), role) {
  set_mtu(kMinLEMTU);
}

void LESignalingChannel::OnConnParamUpdateReceived(
    const SignalingPacket& packet) {
  // Only a LE slave can send this command. "If an LE slave Host receives a
  // Connection Parameter Update Request packet it shall respond with a Command
  // Reject Packet [...]" (v5.0, Vol 3, Part A, Section 4.20).
  if (role() == hci::Connection::Role::kSlave) {
    FXL_VLOG(1)
        << "l2cap: Rejecting Conn. Param. Update request from LE master";
    SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                      common::BufferView());
    return;
  }

  if (packet.payload_size() !=
      sizeof(ConnectionParameterUpdateRequestPayload)) {
    FXL_VLOG(1) << "l2cap: Malformed request received";
    SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                      common::BufferView());
    return;
  }

  const auto& payload =
      packet.payload<ConnectionParameterUpdateRequestPayload>();

  // Reject the connection parameters if they are outside the ranges allowed by
  // the HCI specification (see HCI_LE_Connection_Update command - v5.0, Vol 2,
  // Part E, Section 7.8.18).
  bool reject = false;
  hci::LEPreferredConnectionParameters params(
      le16toh(payload.interval_min), le16toh(payload.interval_max),
      le16toh(payload.slave_latency), le16toh(payload.timeout_multiplier));

  if (params.min_interval() > params.max_interval()) {
    FXL_VLOG(1) << "l2cap: LE conn. min interval larger than max";
    reject = true;
  } else if (params.min_interval() < hci::kLEConnectionIntervalMin) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "l2cap: LE conn. min. interval outside allowed range: 0x%04x",
        params.min_interval());
    reject = true;
  } else if (params.max_interval() > hci::kLEConnectionIntervalMax) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "l2cap: LE conn. max. interval outside allowed range: 0x%04x",
        params.max_interval());
    reject = true;
  } else if (params.max_latency() > hci::kLEConnectionLatencyMax) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "l2cap: LE conn slave latency too big: 0x%04x", params.max_latency());
    reject = true;
  } else if (params.supervision_timeout() <
                 hci::kLEConnectionSupervisionTimeoutMin ||
             params.supervision_timeout() >
                 hci::kLEConnectionSupervisionTimeoutMax) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "l2cap: LE conn supv. timeout outside allowed range: 0x%04x",
        params.supervision_timeout());
    reject = true;
  }

  ConnectionParameterUpdateResult result =
      reject ? ConnectionParameterUpdateResult::kRejected
             : ConnectionParameterUpdateResult::kAccepted;
  ConnectionParameterUpdateResponsePayload rsp;
  rsp.result = static_cast<ConnectionParameterUpdateResult>(htole16(result));
  SendPacket(kConnectionParameterUpdateResponse, packet.header().id,
             common::BufferView(&rsp, sizeof(rsp)));

  if (!reject && conn_param_update_runner_) {
    conn_param_update_runner_->PostTask(
        std::bind(conn_param_update_cb_, params));
  }
}

bool LESignalingChannel::HandlePacket(const SignalingPacket& packet) {
  switch (packet.header().code) {
    case kConnectionParameterUpdateRequest:
      OnConnParamUpdateReceived(packet);
      return true;
    default:
      FXL_VLOG(1) << fxl::StringPrintf("l2cap: LE sig: Unsupported code 0x%02x",
                                       packet.header().code);
      break;
  }

  return false;
}

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
