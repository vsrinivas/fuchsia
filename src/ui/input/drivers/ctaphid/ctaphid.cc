// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ctaphid.h"

#include <endian.h>
#include <fidl/fuchsia.fido.report/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/usages.h>

#include "src/ui/input/drivers/ctaphid/ctaphid_bind.h"

namespace ctaphid {
zx_status_t CtapHidDriver::Start() {
  uint8_t report_desc[HID_MAX_DESC_LEN];
  size_t report_desc_size;
  zx_status_t status = hiddev_.GetDescriptor(report_desc, HID_MAX_DESC_LEN, &report_desc_size);
  if (status != ZX_OK) {
    return status;
  }

  hid::DeviceDescriptor* dev_desc = nullptr;
  hid::ParseResult parse_res = hid::ParseReportDescriptor(report_desc, report_desc_size, &dev_desc);
  if (parse_res != hid::ParseResult::kParseOk) {
    zxlogf(ERROR, "hid-parser: parsing report descriptor failed with error %d", int(parse_res));
    return ZX_ERR_INTERNAL;
  }
  auto free_desc = fit::defer([dev_desc]() { hid::FreeDeviceDescriptor(dev_desc); });

  if (dev_desc->rep_count == 0) {
    zxlogf(ERROR, "No report descriptors found ");
    return ZX_ERR_INTERNAL;
  }

  const hid::ReportDescriptor* desc = &dev_desc->report[0];
  output_packet_size_ = desc->output_byte_sz;
  output_packet_id_ = desc->output_fields->report_id;

  // Payload size calculation taken from the CTAP specification v2.1-ps-20210615 section 11.2.4.
  max_output_data_size_ = output_packet_size_ - INITIALIZATION_PAYLOAD_DATA_OFFSET +
                          MAX_PACKET_SEQ * (output_packet_size_ - CONTINUATION_PAYLOAD_DATA_OFFSET);

  // Register to listen for HID reports.
  status = hiddev_.RegisterListener(this, &hid_report_listener_protocol_ops_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to register for HID reports: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void CtapHidDriver::Stop() { hiddev_.UnregisterListener(); }

zx_status_t CtapHidDriver::Bind() {
  zx_status_t status = Start();
  if (status != ZX_OK) {
    return status;
  }
  status = DdkAdd(ddk::DeviceAddArgs("SecurityKey"));
  if (status != ZX_OK) {
    Stop();
    return status;
  }
  return ZX_OK;
}

void CtapHidDriver::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void CtapHidDriver::DdkRelease() {
  Stop();
  delete this;
}

void CtapHidDriver::SendMessage(SendMessageRequestView request,
                                SendMessageCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  // Check the device is capable of receiving this message's payload size.
  if (request->payload_len() > max_output_data_size_) {
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  // Ensure there is only one outgoing request at a time to maintain transaction atomicity.
  if (pending_response_) {
    completer.ReplyError(ZX_ERR_UNAVAILABLE);
    return;
  }

  // Send the message to the device.
  channel_id_t channel_id = request->channel_id();

  // Divide up the request's data into a series of packets, starting with an initialization packet.
  auto data_it = request->data().begin();
  for (uint8_t packet_seq = INIT_PACKET_SEQ;
       (packet_seq < MAX_PACKET_SEQ || packet_seq == INIT_PACKET_SEQ) &&
       data_it != request->data().end();
       packet_seq++) {
    uint8_t curr_hid_report[HID_MAX_REPORT_LEN] = {0};
    size_t n_bytes = 0;

    // Write the Channel ID.
    curr_hid_report[CHANNEL_ID_OFFSET + 0] = (channel_id >> 24) & 0xFF;
    curr_hid_report[CHANNEL_ID_OFFSET + 1] = (channel_id >> 16) & 0xFF;
    curr_hid_report[CHANNEL_ID_OFFSET + 2] = (channel_id >> 8) & 0xFF;
    curr_hid_report[CHANNEL_ID_OFFSET + 3] = channel_id & 0xFF;

    // Write the rest of the packet header. This differs between initialization and continuation
    // packets.
    if (packet_seq == INIT_PACKET_SEQ) {
      // Write the Command ID with the initialization packet bit set.
      curr_hid_report[COMMAND_ID_OFFSET] =
          fidl::ToUnderlying(request->command_id()) | INIT_PACKET_BIT;
      // Write the Payload Length
      curr_hid_report[PAYLOAD_LEN_HI_OFFSET] = (request->payload_len() >> 8) & 0xFF;
      curr_hid_report[PAYLOAD_LEN_LO_OFFSET] = request->payload_len() & 0xFF;

      n_bytes = INITIALIZATION_PAYLOAD_DATA_OFFSET;
    } else {
      // The packet sequence value, starting at 0.
      curr_hid_report[PACKET_SEQ_OFFSET] = packet_seq;

      n_bytes = CONTINUATION_PAYLOAD_DATA_OFFSET;
    }

    // Write the payload.
    for (; n_bytes < output_packet_size_ && data_it != request->data().end(); n_bytes++) {
      curr_hid_report[n_bytes] = *data_it;
      data_it++;
    }

    zx_status_t status = hiddev_.SetReport(HID_REPORT_TYPE_OUTPUT, output_packet_id_,
                                           curr_hid_report, output_packet_size_);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
  }

  // Set the pending response. The pending response will be reset once the device has sent a
  // response and it has been retrieved via GetMessage().
  // TODO(fxbug.dev/103893): have this clear after some time or when the list gets too large.
  pending_response_ = pending_response{
      .channel = channel_id,
      .next_packet_seq_expected = INIT_PACKET_SEQ,
  };

  completer.ReplySuccess();
}

void CtapHidDriver::GetMessage(GetMessageRequestView request,
                               GetMessageCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);

  if (pending_response_->channel == request->channel_id) {
    if (pending_response_->waiting_read) {
      completer.ReplyError(ZX_ERR_ALREADY_BOUND);
      return;
    }

    pending_response_->waiting_read = completer.ToAsync();
    ReplyToWaitingGetMessage();

    return;
  }

  // If no matching response or pending request was found, either the response had timed out or no
  // matching request had been made.
  completer.ReplyError(ZX_ERR_NOT_FOUND);
}

void CtapHidDriver::ReplyToWaitingGetMessage() {
  if (!pending_response_->last_packet_received_time.has_value()) {
    // We are still waiting on a response.
    return;
  }

  auto response_builder_ = fuchsia_fido_report::wire::Message::Builder(response_allocator_);
  response_builder_.channel_id(pending_response_->channel);
  response_builder_.command_id(
      fuchsia_fido_report::CtapHidCommand(pending_response_->command.value()));
  response_builder_.payload_len(pending_response_->payload_len.value());
  response_builder_.data(fidl::VectorView<uint8_t>::FromExternal(pending_response_->data));
  pending_response_->waiting_read->ReplySuccess(response_builder_.Build());
  fidl::Status result = pending_response_->waiting_read->result_of_reply();
  if (!result.ok()) {
    zxlogf(ERROR, "GetMessage: Failed to get message: %s\n", result.FormatDescription().c_str());
  }
  pending_response_->waiting_read.reset();
  response_allocator_.Reset();

  // Remove the pending response if this is not a KEEPALIVE message, as KEEPALIVE
  // messages are not considered an actual response to any command sent to the keys.
  if (pending_response_->command !=
      fidl::ToUnderlying(fuchsia_fido_report::CtapHidCommand::kKeepalive)) {
    pending_response_.reset();
  }
}

void CtapHidDriver::HidReportListenerReceiveReport(const uint8_t* report, size_t report_size,
                                                   zx_time_t report_time) {
  fbl::AutoLock lock(&lock_);

  channel_id_t current_channel =
      (report[0] << 24) | (report[1] << 16) | (report[2] << 8) | report[3];
  uint8_t data_size = static_cast<uint8_t>(report_size);

  if (pending_response_->channel != current_channel) {
    // This means that we have received an unexpected as there is no pending request on this channel
    // as far as the driver is aware. Ignore this packet.
    return;
  }

  if (report[COMMAND_ID_OFFSET] & INIT_PACKET_BIT) {
    if (pending_response_->next_packet_seq_expected != INIT_PACKET_SEQ &&
        pending_response_->command !=
            fidl::ToUnderlying(fuchsia_fido_report::CtapHidCommand::kKeepalive)) {
      // Unexpected sequence. We must be out of sync.
      // Write an invalid sequence error response to the corresponding pending response.
      pending_response_->command = fidl::ToUnderlying(fuchsia_fido_report::CtapHidCommand::kError);
      pending_response_->bytes_received = 1;
      pending_response_->payload_len = 1;
      pending_response_->data = std::vector<uint8_t>(CtaphidErr::InvalidSeq);
      pending_response_->last_packet_received_time.emplace(report_time);

      return;
    }

    command_id_t command_id = report[COMMAND_ID_OFFSET] & ~INIT_PACKET_BIT;
    data_size -= INITIALIZATION_PAYLOAD_DATA_OFFSET;

    pending_response_->bytes_received = data_size;
    pending_response_->payload_len = report[PAYLOAD_LEN_HI_OFFSET] << 8;
    pending_response_->payload_len =
        pending_response_->payload_len.value() | report[PAYLOAD_LEN_LO_OFFSET];

    pending_response_->data =
        std::vector<uint8_t>(&report[INITIALIZATION_PAYLOAD_DATA_OFFSET],
                             &report[INITIALIZATION_PAYLOAD_DATA_OFFSET] + data_size);
    pending_response_->command = command_id;
    pending_response_->next_packet_seq_expected = MIN_PACKET_SEQ;

  } else {
    auto current_packet_sequence = report[PACKET_SEQ_OFFSET];
    data_size -= CONTINUATION_PAYLOAD_DATA_OFFSET;
    if (current_packet_sequence != pending_response_->next_packet_seq_expected) {
      // Unexpected sequence. We must be out of sync.
      // Write an invalid sequence error response to the corresponding pending response.
      pending_response_->command = fidl::ToUnderlying(fuchsia_fido_report::CtapHidCommand::kError);
      pending_response_->bytes_received = 1;
      pending_response_->payload_len = 1;
      pending_response_->data = std::vector<uint8_t>(CtaphidErr::InvalidSeq);
      pending_response_->last_packet_received_time.emplace(report_time);

      return;
    }

    pending_response_->data.insert(pending_response_->data.end(),
                                   &report[CONTINUATION_PAYLOAD_DATA_OFFSET],
                                   &report[CONTINUATION_PAYLOAD_DATA_OFFSET] + data_size);
    pending_response_->bytes_received += data_size;
    pending_response_->next_packet_seq_expected += 1;
  }

  if (pending_response_->bytes_received >= pending_response_->payload_len) {
    // We have finished receiving packets for this response.
    pending_response_->last_packet_received_time.emplace(report_time);
  }
}

zx_status_t ctaphid_bind(void* ctx, zx_device_t* parent) {
  ddk::HidDeviceProtocolClient hiddev(parent);
  if (!hiddev.is_valid()) {
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<CtapHidDriver>(&ac, parent, hiddev);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static zx_driver_ops_t ctaphid_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ctaphid_bind;
  return ops;
}();

}  // namespace ctaphid

ZIRCON_DRIVER(ctaphid, ctaphid::ctaphid_driver_ops, "zircon", "0.1");
