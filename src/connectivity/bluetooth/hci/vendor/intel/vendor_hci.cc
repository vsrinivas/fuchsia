// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vendor_hci.h"

#include <lib/zx/clock.h>
#include <lib/zx/object.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>

#include <fbl/algorithm.h>

#include "logging.h"

namespace btintel {

using ::bt::hci::CommandPacket;

namespace {

constexpr size_t kMaxSecureSendArgLen = 252;
constexpr auto kInitTimeoutMs = zx::sec(10);

}  // namespace

VendorHci::VendorHci(zx::channel* ctrl) : ctrl_(ctrl), acl_(nullptr), manufacturer_(false) {}

ReadVersionReturnParams VendorHci::SendReadVersion() const {
  auto packet = CommandPacket::New(kReadVersion);
  SendCommand(packet->view());
  auto evt_packet = WaitForEventPacket();
  if (evt_packet) {
    auto params = evt_packet->return_params<ReadVersionReturnParams>();
    if (params)
      return *params;
  }
  errorf("VendorHci: ReadVersion: Error reading response!\n");
  return ReadVersionReturnParams{.status = bt::hci::StatusCode::kUnspecifiedError};
}

ReadBootParamsReturnParams VendorHci::SendReadBootParams() const {
  auto packet = CommandPacket::New(kReadBootParams);
  SendCommand(packet->view());
  auto evt_packet = WaitForEventPacket();
  if (evt_packet) {
    auto params = evt_packet->return_params<ReadBootParamsReturnParams>();
    if (params)
      return *params;
  }
  errorf("VendorHci: ReadBootParams: Error reading response!\n");
  return ReadBootParamsReturnParams{.status = bt::hci::StatusCode::kUnspecifiedError};
}

bt::hci::StatusCode VendorHci::SendHciReset() const {
  auto packet = CommandPacket::New(bt::hci::kReset);
  SendCommand(packet->view());

  // TODO(armansito): Consider collecting a metric for initialization time
  // (successful and failing) to provide us with a better sense of how long
  // these timeouts should be.
  auto evt_packet = WaitForEventPacket(kInitTimeoutMs, bt::hci::kCommandCompleteEventCode);
  if (!evt_packet) {
    errorf("VendorHci: failed while waiting for HCI_Reset response\n");
    return bt::hci::StatusCode::kUnspecifiedError;
  }

  const auto* params = evt_packet->return_params<bt::hci::SimpleReturnParams>();
  if (!params) {
    errorf("VendorHci: HCI_Reset: received malformed response\n");
    return bt::hci::StatusCode::kUnspecifiedError;
  }

  return params->status;
}

void VendorHci::SendVendorReset() const {
  auto packet = CommandPacket::New(kReset, sizeof(ResetCommandParams));
  auto params = packet->mutable_payload<ResetCommandParams>();
  params->data[0] = 0x00;
  params->data[1] = 0x01;
  params->data[2] = 0x00;
  params->data[3] = 0x01;
  params->data[4] = 0x00;
  params->data[5] = 0x08;
  params->data[6] = 0x04;
  params->data[7] = 0x00;

  SendCommand(packet->view());

  // Sleep for 2 seconds to let the controller process the reset.
  zx_nanosleep(zx_deadline_after(ZX_SEC(2)));
}

bool VendorHci::SendSecureSend(uint8_t type, const bt::BufferView& bytes) const {
  size_t left = bytes.size();
  while (left > 0) {
    size_t frag_len = std::min(left, kMaxSecureSendArgLen);
    auto cmd = CommandPacket::New(kSecureSend, frag_len + 1);
    auto data = cmd->mutable_view()->mutable_payload_data();
    data[0] = type;
    data.Write(bytes.view(bytes.size() - left, frag_len), 1);

    SendCommand(cmd->view());
    std::unique_ptr<bt::hci::EventPacket> event = WaitForEventPacket();
    if (!event) {
      errorf("VendorHci: SecureSend: Error reading response!\n");
      return false;
    }
    if (event->event_code() == bt::hci::kCommandCompleteEventCode) {
      const auto& event_params = event->view().payload<bt::hci::CommandCompleteEventParams>();
      if (le16toh(event_params.command_opcode) != kSecureSend) {
        errorf("VendorHci: Received command complete for something else!\n");
      } else if (event_params.return_parameters[0] != 0x00) {
        errorf("VendorHci: Received 0x%x instead of zero in command complete!\n",
               event_params.return_parameters[0]);
        return false;
      }
    } else if (event->event_code() == bt::hci::kVendorDebugEventCode) {
      const auto& params = event->view().template payload<SecureSendEventParams>();
      infof("VendorHci: SecureSend result 0x%x, opcode: 0x%x, status: 0x%x\n", params.result,
            params.opcode, params.status);
      if (params.result) {
        errorf("VendorHci: Result of %d indicates some error!\n", params.result);
        return false;
      }
    }
    left -= frag_len;
  }
  return true;
}

bool VendorHci::SendAndExpect(const bt::PacketView<bt::hci::CommandHeader>& command,
                              std::deque<bt::BufferView> events) const {
  SendCommand(command);

  while (events.size() > 0) {
    auto evt_packet = WaitForEventPacket();
    if (!evt_packet) {
      return false;
    }
    auto expected = events.front();
    if ((evt_packet->view().size() != expected.size()) ||
        (memcmp(evt_packet->view().data().data(), expected.data(), expected.size()) != 0)) {
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

  auto packet = CommandPacket::New(kMfgModeChange, sizeof(MfgModeChangeCommandParams));
  auto params = packet->mutable_payload<MfgModeChangeCommandParams>();
  params->enable = bt::hci::GenericEnableParam::kEnable;
  params->disable_mode = MfgDisableMode::kNoPatches;

  SendCommand(packet->view());
  auto evt_packet = WaitForEventPacket();
  if (!evt_packet || evt_packet->event_code() != bt::hci::kCommandCompleteEventCode) {
    errorf("VendorHci: EnterManufacturerMode failed");
    return;
  }

  manufacturer_ = true;
}

bool VendorHci::ExitManufacturerMode(MfgDisableMode mode) {
  if (!manufacturer_)
    return false;

  manufacturer_ = false;

  auto packet = CommandPacket::New(kMfgModeChange, sizeof(MfgModeChangeCommandParams));
  auto params = packet->mutable_payload<MfgModeChangeCommandParams>();
  params->enable = bt::hci::GenericEnableParam::kDisable;
  params->disable_mode = mode;

  SendCommand(packet->view());
  auto evt_packet = WaitForEventPacket();
  if (!evt_packet || evt_packet->event_code() != bt::hci::kCommandCompleteEventCode) {
    errorf("VendorHci: ExitManufacturerMode failed");
    return false;
  }

  return true;
}

void VendorHci::SendCommand(const bt::PacketView<bt::hci::CommandHeader>& command) const {
  zx_status_t status = ctrl_->write(0, command.data().data(), command.size(), nullptr, 0);
  if (status < 0) {
    errorf("VendorHci: SendCommand failed: %s\n", zx_status_get_string(status));
  }
}

std::unique_ptr<bt::hci::EventPacket> VendorHci::WaitForEventPacket(
    zx::duration timeout, bt::hci::EventCode expected_event) const {
  zx_wait_item_t wait_items[2];
  uint32_t wait_item_count = 1;

  ZX_DEBUG_ASSERT(ctrl_);
  wait_items[0].handle = ctrl_->get();
  wait_items[0].waitfor = ZX_CHANNEL_READABLE;
  wait_items[0].pending = 0;

  if (acl_) {
    wait_items[1].handle = acl_->get();
    wait_items[1].waitfor = ZX_CHANNEL_READABLE;
    wait_items[1].pending = 0;
    wait_item_count++;
  }

  auto begin = zx::clock::get_monotonic();
  for (zx::duration elapsed; elapsed < timeout; elapsed = zx::clock::get_monotonic() - begin) {
    zx_status_t status = zx_object_wait_many(wait_items, wait_item_count,
                                             zx::deadline_after(timeout - elapsed).get());
    if (status != ZX_OK) {
      errorf("VendorHci: channel error: %s\n", zx_status_get_string(status));
      return nullptr;
    }

    // Determine which channel caused the event.
    zx_handle_t evt_handle = 0;
    for (unsigned i = 0; i < wait_item_count; ++i) {
      if (wait_items[i].pending & ZX_CHANNEL_READABLE) {
        evt_handle = wait_items[0].handle;
        break;
      }
    }

    ZX_DEBUG_ASSERT(evt_handle);

    // Allocate a buffer for the event. We don't know the size
    // beforehand we allocate the largest possible buffer.
    auto packet = bt::hci::EventPacket::New(bt::hci::kMaxCommandPacketPayloadSize);
    if (!packet) {
      errorf("VendorHci: Failed to allocate event packet!\n");
      return nullptr;
    }

    uint32_t read_size;
    auto packet_bytes = packet->mutable_view()->mutable_data();
    zx_status_t read_status = zx_channel_read(evt_handle, 0u, packet_bytes.mutable_data(), nullptr,
                                              packet_bytes.size(), 0, &read_size, nullptr);
    if (read_status < 0) {
      errorf("VendorHci: Failed to read event bytes: %s\n", zx_status_get_string(read_status));
      return nullptr;
    }

    if (read_size < sizeof(bt::hci::EventHeader)) {
      errorf("VendorHci: Malformed event packet expected >%zu bytes, got %d\n",
             sizeof(bt::hci::EventHeader), read_size);
      return nullptr;
    }

    // Compare the received payload size to what is in the header.
    const size_t rx_payload_size = read_size - sizeof(bt::hci::EventHeader);
    const size_t size_from_header = packet->view().header().parameter_total_size;
    if (size_from_header != rx_payload_size) {
      errorf(
          "VendorHci: Malformed event packet - header payload size (%zu) != "
          "received (%zu)\n",
          size_from_header, rx_payload_size);
      return nullptr;
    }

    if (expected_event && expected_event != packet->event_code()) {
      tracef("VendorHci: keep waiting (expected: 0x%02x, got: 0x%02x)\n", expected_event,
             packet->event_code());
      continue;
    }

    packet->InitializeFromBuffer();
    return packet;
  }

  errorf("VendorHci: timed out waiting for event\n");
  return nullptr;
}

}  // namespace btintel
