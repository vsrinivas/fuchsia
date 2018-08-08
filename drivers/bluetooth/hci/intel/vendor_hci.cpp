// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vendor_hci.h"
#include "logging.h"

#include <fbl/algorithm.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

namespace btintel {

using ::btlib::hci::CommandPacket;

namespace {

constexpr size_t kMaxSecureSendArgLen = 252;

}  // namespace

VendorHci::VendorHci(zx::channel* channel)
    : channel_(channel), manufacturer_(false){};

ReadVersionReturnParams VendorHci::SendReadVersion() const {
  auto packet = CommandPacket::New(kReadVersion);
  SendCommand(packet->view());
  auto evt_packet = ReadEventPacket();
  if (evt_packet) {
    auto params = evt_packet->return_params<ReadVersionReturnParams>();
    if (params)
      return *params;
  }
  errorf("VendorHci: ReadVersion: Error reading response!\n");
  return ReadVersionReturnParams{.status =
                                     btlib::hci::StatusCode::kUnspecifiedError};
}

ReadBootParamsReturnParams VendorHci::SendReadBootParams() const {
  auto packet = CommandPacket::New(kReadBootParams);
  SendCommand(packet->view());
  auto evt_packet = ReadEventPacket();
  if (evt_packet) {
    auto params = evt_packet->return_params<ReadBootParamsReturnParams>();
    if (params)
      return *params;
  }
  errorf("VendorHci: ReadBootParams: Error reading response!\n");
  return ReadBootParamsReturnParams{
      .status = btlib::hci::StatusCode::kUnspecifiedError};
}

void VendorHci::SendReset() const {
  auto packet = CommandPacket::New(kReset, sizeof(ResetCommandParams));
  auto params = packet->mutable_view()->mutable_payload<ResetCommandParams>();
  params->data[0] = 0x00;
  params->data[1] = 0x01;
  params->data[2] = 0x00;
  params->data[3] = 0x01;
  params->data[4] = 0x00;
  params->data[5] = 0x08;
  params->data[6] = 0x04;
  params->data[7] = 0x00;

  SendCommand(packet->view());
  // Don't expect a return here.
}

bool VendorHci::SendSecureSend(uint8_t type,
                               const btlib::common::BufferView& bytes) const {
  size_t left = bytes.size();
  while (left > 0) {
    size_t frag_len = fbl::min(left, kMaxSecureSendArgLen);
    auto cmd = CommandPacket::New(kSecureSend, frag_len + 1);
    auto data = cmd->mutable_view()->mutable_payload_data();
    data[0] = type;
    data.Write(bytes.view(bytes.size() - left, frag_len), 1);

    SendCommand(cmd->view());
    auto event = ReadEventPacket();
    if (!event) {
      errorf("VendorHci: SecureSend: Error reading response!\n");
      return false;
    }
    if (event->event_code() == btlib::hci::kCommandCompleteEventCode) {
      const auto& event_params =
          event->view()
              .template payload<btlib::hci::CommandCompleteEventParams>();
      if (le16toh(event_params.command_opcode) != kSecureSend) {
        errorf("VendorHci: Received command complete for something else!\n");
      } else if (event_params.return_parameters[0] != 0x00) {
        errorf(
            "VendorHci: Received 0x%x instead of zero in command complete!\n",
            event_params.return_parameters[0]);
        return false;
      }
    } else if (event->event_code() == btlib::hci::kVendorDebugEventCode) {
      const auto& params =
          event->view().template payload<SecureSendEventParams>();
      infof("VendorHci: SecureSend result 0x%x, opcode: 0x%x, status: 0x%x\n",
            params.result, params.opcode, params.status);
      if (params.result) {
        errorf("VendorHci: Result of %d indicates some error!\n",
               params.result);
        return false;
      }
    }
    left -= frag_len;
  }
  return true;
}

bool VendorHci::SendAndExpect(
    const btlib::common::PacketView<btlib::hci::CommandHeader>& command,
    std::deque<btlib::common::BufferView> events) const {
  SendCommand(command);

  while (events.size() > 0) {
    auto evt_packet = ReadEventPacket();
    if (!evt_packet) {
      return false;
    }
    auto expected = events.front();
    if ((evt_packet->view().size() != expected.size()) ||
        (memcmp(evt_packet->view().data().data(), expected.data(),
                expected.size()) != 0)) {
      errorf("VendorHci: SendAndExpect: unexpected event received\n");
      return false;
    }
    events.pop_front();
  }

  return true;
}

void VendorHci::EnterManufacturerMode() {
  if (manufacturer_)
    return;

  auto packet =
      CommandPacket::New(kMfgModeChange, sizeof(MfgModeChangeCommandParams));
  auto params =
      packet->mutable_view()->mutable_payload<MfgModeChangeCommandParams>();
  params->enable = btlib::hci::GenericEnableParam::kEnable;
  params->disable_mode = MfgDisableMode::kNoPatches;

  SendCommand(packet->view());
  auto evt_packet = ReadEventPacket();
  if (!evt_packet ||
      evt_packet->event_code() != btlib::hci::kCommandCompleteEventCode) {
    errorf("VendorHci: EnterManufacturerMode failed");
    return;
  }

  manufacturer_ = true;
}

bool VendorHci::ExitManufacturerMode(MfgDisableMode mode) {
  if (!manufacturer_)
    return false;

  manufacturer_ = false;

  auto packet =
      CommandPacket::New(kMfgModeChange, sizeof(MfgModeChangeCommandParams));
  auto params =
      packet->mutable_view()->mutable_payload<MfgModeChangeCommandParams>();
  params->enable = btlib::hci::GenericEnableParam::kDisable;
  params->disable_mode = mode;

  SendCommand(packet->view());
  auto evt_packet = ReadEventPacket();
  if (!evt_packet ||
      evt_packet->event_code() != btlib::hci::kCommandCompleteEventCode) {
    errorf("VendorHci: ExitManufacturerMode failed");
    return false;
  }

  return true;
}

void VendorHci::SendCommand(
    const btlib::common::PacketView<btlib::hci::CommandHeader>& command) const {
  zx_status_t status =
      channel_->write(0, command.data().data(), command.size(), nullptr, 0);
  if (status < 0) {
    errorf("VendorHci: SendCommand failed: %s\n", zx_status_get_string(status));
  }
}

std::unique_ptr<btlib::hci::EventPacket> VendorHci::ReadEventPacket() const {
  zx_signals_t observed;
  zx_status_t status = channel_->wait_one(
      ZX_CHANNEL_READABLE, zx::deadline_after(zx::sec(1)), &observed);

  if (status != ZX_OK) {
    errorf("VendorHci: channel error: %s\n", zx_status_get_string(status));
    return nullptr;
  }

  FXL_DCHECK(observed & ZX_CHANNEL_READABLE);

  // Allocate a buffer for the event. We don't know the size
  // beforehand we allocate the largest possible buffer.
  auto packet =
      btlib::hci::EventPacket::New(btlib::hci::kMaxCommandPacketPayloadSize);
  if (!packet) {
    errorf("VendorHci: Failed to allocate event packet!\n");
    return nullptr;
  }

  uint32_t read_size;
  auto packet_bytes = packet->mutable_view()->mutable_data();
  zx_status_t read_status =
      channel_->read(0u, packet_bytes.mutable_data(), packet_bytes.size(),
                     &read_size, nullptr, 0, nullptr);
  if (read_status < 0) {
    errorf("VendorHci: Failed to read event bytes: %s\n",
           zx_status_get_string(read_status));
    return nullptr;
  }

  if (read_size < sizeof(btlib::hci::EventHeader)) {
    errorf("VendorHci: Malformed event packet expected >%zu bytes, got %d\n",
           sizeof(btlib::hci::EventHeader), read_size);
    return nullptr;
  }

  // Compare the received payload size to what is in the header.
  const size_t rx_payload_size = read_size - sizeof(btlib::hci::EventHeader);
  const size_t size_from_header = packet->view().header().parameter_total_size;
  if (size_from_header != rx_payload_size) {
    errorf(
        "VendorHci: Malformed event packet - header payload size (%zu) != "
        "received (%zu)\n",
        size_from_header, rx_payload_size);
    return nullptr;
  }

  packet->InitializeFromBuffer();

  return packet;
}

}  // namespace btintel
