// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_CTAPHID_CTAPHID_H_
#define SRC_UI_INPUT_DRIVERS_CTAPHID_CTAPHID_H_

#include <fidl/fuchsia.fido.report/cpp/wire.h>
#include <fuchsia/hardware/hiddevice/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <map>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

namespace ctaphid {
using channel_id_t = uint32_t;
using command_id_t = uint8_t;
using GetMessageCompleter =
    ::fidl::internal::WireCompleter<::fuchsia_fido_report::SecurityKeyDevice::GetMessage>;

// The following are CTAPHID error codes from the CTAP specification v2.1-ps-20210615
// section 11.2.9.1.6.
enum CtaphidErr {
  InvalidCmd = 0x01,  //	The command in the request is invalid
  InvalidPar = 0x02,  //	The parameter(s) in the request is invalid
  InvalidLen = 0x03,  //	The length field (BCNT) is invalid for the request
  InvalidSeq = 0x04,  //	The sequence does not match expected value
  MsgTimeout = 0x05,  //	The message has timed out
  ChannelBusy =
      0x06,  //	The device is busy for the requesting channel. The client SHOULD retry the request
             // after a short delay. Note that the client MAY abort the transaction if the command
             // is no longer relevant.
  LockRequired = 0x0A,    //	Command requires channel lock
  InvalidChannel = 0x0B,  //	CID is not valid.
  Other = 0x7F,           //	Unspecified error
};

using pending_response = struct pending_response {
  // The channel we are waiting on a response from.
  channel_id_t channel;

  // The fields to be set by the response.
  std::optional<command_id_t> command;
  // This is also the expected number of bytes to be received for the current response.
  std::optional<uint16_t> payload_len;
  std::vector<uint8_t> data;

  // Keep track of the number of bytes received so far for the response.
  uint16_t bytes_received = 0;

  // The time the last packet of this response was received.
  std::optional<zx_time_t> last_packet_received_time;
  // The next expected sequence value of a continuation packet.
  uint8_t next_packet_seq_expected;

  // Keeps a reference to a pending request if GetMessage is called on this channel before
  // the response has been sent from the key.
  std::optional<GetMessageCompleter::Async> waiting_read;
};

class CtapHidDriver;
using CtapHidDriverDeviceType =
    ddk::Device<CtapHidDriver, ddk::Unbindable,
                ddk::Messageable<fuchsia_fido_report::SecurityKeyDevice>::Mixin>;
class CtapHidDriver : public CtapHidDriverDeviceType,
                      public ddk::EmptyProtocol<ZX_PROTOCOL_CTAP>,
                      ddk::HidReportListenerProtocol<CtapHidDriver> {
 public:
  CtapHidDriver(zx_device_t* parent, ddk::HidDeviceProtocolClient hiddev)
      : CtapHidDriverDeviceType(parent), hiddev_(hiddev) {}

  ~CtapHidDriver() {
    fbl::AutoLock lock(&lock_);
    if (pending_response_->waiting_read) {
      pending_response_->waiting_read->ReplyError(ZX_ERR_PEER_CLOSED);
      pending_response_->waiting_read.reset();
    }
  }

  zx_status_t Start();
  void Stop();

  // DDK Functions.
  zx_status_t Bind();
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // FIDL functions.
  void SendMessage(SendMessageRequestView request, SendMessageCompleter::Sync& completer) override;
  void GetMessage(GetMessageRequestView request, GetMessageCompleter::Sync& completer) override;

  // HidReportListener functions.
  void HidReportListenerReceiveReport(const uint8_t* report, size_t report_size,
                                      zx_time_t report_time);

 private:
  static constexpr size_t kFidlReportBufferSize = 8192;

  // The index of the first byte of the payload in an initialization packet.
  static constexpr uint8_t INITIALIZATION_PAYLOAD_DATA_OFFSET = 7;
  // The index of the first byte of the payload in a continuation packet.
  static constexpr uint8_t CONTINUATION_PAYLOAD_DATA_OFFSET = 5;
  // The maximum number of packets a payload can be divided into, as per the CTAP spec.
  static constexpr uint8_t MIN_PACKET_SEQ = 0x00;
  static constexpr uint8_t MAX_PACKET_SEQ = 0x7f;
  // The first packet send to a device follows the structure of an initialization packet.
  static constexpr uint8_t INIT_PACKET_SEQ = 0xff;
  // MSB is set for the 5th byte of Initialisation packets.
  static constexpr uint8_t INIT_PACKET_BIT = (1u << 7);
  // Indices of the remaining packet fields.
  static constexpr uint8_t CHANNEL_ID_OFFSET = 0;
  static constexpr uint8_t COMMAND_ID_OFFSET = 4;
  static constexpr uint8_t PACKET_SEQ_OFFSET = 4;
  static constexpr uint8_t PAYLOAD_LEN_HI_OFFSET = 5;
  static constexpr uint8_t PAYLOAD_LEN_LO_OFFSET = 6;

  void ReplyToWaitingGetMessage() __TA_REQUIRES(lock_);

  void CreatePacketHeader(uint8_t packet_sequence, uint32_t channel_id,
                          fuchsia_fido_report::CtapHidCommand command_id, uint16_t payload_len,
                          uint8_t* out, size_t out_size);

  ddk::HidDeviceProtocolClient hiddev_;

  fbl::Mutex lock_;
  fidl::Arena<kFidlReportBufferSize> response_allocator_ __TA_GUARDED(lock_);

  // Fields for the output packets to be received from devices.
  uint8_t output_packet_id_ = 0;
  size_t output_packet_size_ = 0;
  size_t max_output_data_size_ = 0;

  // Currently awaiting response.
  std::optional<pending_response> pending_response_ __TA_GUARDED(lock_);
};

}  // namespace ctaphid

#endif  // SRC_UI_INPUT_DRIVERS_CTAPHID_CTAPHID_H_
