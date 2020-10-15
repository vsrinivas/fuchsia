// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-device.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdio>
#include <future>
#include <string>

#include <ddktl/fidl.h>

namespace fidl_tel_snoop = ::llcpp::fuchsia::telephony::snoop;

namespace at_fake {
static const std::string kAtCmdReqAtdStr = "ATD";
static const std::string kAtCmdRespNoCarrier = "NO CARRIER\r";
static const std::string kAtCmdReqAtCgmi = "AT+CGMI\r";
static const std::string kAtCmdRespManuId = "Sierra Wireless Incorporated\r\rOK\r";
static const std::string kAtCmdRespErr = "ERROR\r";

static const size_t kRaceDetectionPrefixSize = 4;
static const std::string kRaceDetectionRequestPrefix = "RACE";
static const std::string kRaceDetectionResponsePrefix = "ECAR";

static const std::string kInvalidUnicodeRequest = "INVALID UNICODE";
static const size_t kInvalidUnicodeResponseSize = 2;
static const char* kInvalidUnicodeResponse = "\x80\x81";

constexpr uint32_t kTelCtrlPlanePktMax = 2048;

AtDevice::AtDevice(zx_device_t* device) : Device(device) {}

#define DEV(c) static_cast<AtDevice*>(c)
static zx_protocol_device_t at_fake_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = [](void* ctx, uint32_t proto_id, void* out_proto) -> zx_status_t {
      return DEV(ctx)->GetProtocol(proto_id, out_proto);
    },
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) -> zx_status_t {
      return DEV(ctx)->DdkMessage(msg, txn);
    },
};
#undef DEV

static void sent_fake_at_msg(zx::channel& channel, uint8_t* resp, uint32_t resp_size) {
  zx_status_t status;
  status = channel.write(0, resp, resp_size, NULL, 0);
  if (status < 0) {
    zxlogf(ERROR, "at-fake-transport: failed to write message to channel: %s",
           zx_status_get_string(status));
  }
}

void AtDevice::SnoopCtrlMsg(uint8_t* snoop_data, uint32_t snoop_data_len,
                            fidl_tel_snoop::Direction direction) {
  if (GetCtrlSnoopChannel()) {
    fidl_tel_snoop::Message snoop_msg;
    fidl_tel_snoop::QmiMessage msg;
    uint32_t current_length =
        std::min(static_cast<std::size_t>(snoop_data_len), sizeof(msg.opaque_bytes));
    msg.is_partial_copy = snoop_data_len > current_length;
    msg.direction = direction;
    msg.timestamp = zx_clock_get_monotonic();
    memcpy(msg.opaque_bytes.data_, snoop_data, current_length);
    snoop_msg.set_qmi_message(fidl::unowned_ptr(&msg));
    zxlogf(INFO, "at-fake-transport: snoop msg %u %u %u %u sent", msg.opaque_bytes.data_[0],
           msg.opaque_bytes.data_[1], msg.opaque_bytes.data_[2], msg.opaque_bytes.data_[3]);
    fidl_tel_snoop::Publisher::Call::SendMessage(zx::unowned_channel(GetCtrlSnoopChannel().get()),
                                                 std::move(snoop_msg));
  }
}

void AtDevice::ReplyCtrlMsg(uint8_t* req, uint32_t req_size, uint8_t* resp, uint32_t resp_size) {
  zxlogf(INFO, "at-fake-driver: req %u %u %u %u with len %u", req[0], req[1], req[2], req[3],
         req_size);
  if (0 == memcmp(req, kAtCmdReqAtdStr.c_str(), kAtCmdReqAtdStr.size())) {
    resp_size = std::min(kAtCmdRespNoCarrier.size(), static_cast<std::size_t>(resp_size));
    memcpy(resp, kAtCmdRespNoCarrier.c_str(), resp_size);
    zxlogf(INFO, "at-fake-driver: resp %u %u %u %u with len %u", resp[0], resp[1], resp[2], resp[3],
           resp_size);
    sent_fake_at_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);

  } else if (0 == memcmp(req, kAtCmdReqAtCgmi.c_str(), kAtCmdReqAtCgmi.size())) {
    resp_size = std::min(kAtCmdRespManuId.size(), static_cast<std::size_t>(resp_size));
    memcpy(resp, kAtCmdRespManuId.c_str(), resp_size);
    sent_fake_at_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);

  } else if (0 == memcmp(req, kInvalidUnicodeRequest.c_str(), kInvalidUnicodeRequest.size())) {
    resp_size = std::min(kInvalidUnicodeResponseSize, static_cast<std::size_t>(resp_size));
    memcpy(resp, kInvalidUnicodeResponse, resp_size);
    sent_fake_at_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);

  } else if (req_size >= kRaceDetectionPrefixSize &&
             0 == memcmp(req, kRaceDetectionRequestPrefix.c_str(), kRaceDetectionPrefixSize)) {
    resp_size = std::min(resp_size, req_size);
    memcpy(resp, kRaceDetectionResponsePrefix.c_str(), kRaceDetectionPrefixSize);
    memcpy(resp + kRaceDetectionPrefixSize, req + kRaceDetectionPrefixSize,
           req_size - kRaceDetectionPrefixSize);
    sent_fake_at_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);

  } else {
    zxlogf(ERROR, "at-fake-driver: unexpected at msg received");
    resp_size = std::min(kAtCmdRespErr.size(), static_cast<std::size_t>(resp_size));
    memcpy(resp, kAtCmdRespErr.c_str(), resp_size);
    sent_fake_at_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
  }
}

static int at_fake_transport_thread(void* cookie) {
  assert(cookie != NULL);
  AtDevice* device_ptr = static_cast<AtDevice*>(cookie);
  uint32_t req_len = 0;
  uint8_t req_buf[kTelCtrlPlanePktMax];
  uint8_t resp_buf[kTelCtrlPlanePktMax];

  zx_port_packet_t packet;
  zxlogf(INFO, "at-fake-transport: event loop initialized");
  while (true) {
    zx_status_t status = device_ptr->GetCtrlChannelPort().wait(zx::time::infinite(), &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "at-fake-transport: timed out: %s", zx_status_get_string(status));
    } else if (status == ZX_OK) {
      switch (packet.key) {
        case tel_fake::kChannelMsg:
          if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
            zxlogf(ERROR, "at-fake-transport: channel closed");
            status = device_ptr->CloseCtrlChannel();
            continue;
          }
          status = device_ptr->GetCtrlChannel().read(0, req_buf, NULL, kTelCtrlPlanePktMax, 0,
                                                     &req_len, NULL);
          if (status != ZX_OK) {
            zxlogf(ERROR, "at-fake-transport: failed to read channel: %s",
                   zx_status_get_string(status));
            return status;
          }
          device_ptr->SnoopCtrlMsg(req_buf, kTelCtrlPlanePktMax,
                                   fidl_tel_snoop::Direction::TO_MODEM);
          // TODO (jiamingw): parse AT msg, form reply and write back to channel.
          device_ptr->ReplyCtrlMsg(req_buf, req_len, resp_buf, kTelCtrlPlanePktMax);
          status = device_ptr->SetAsyncWait();
          if (status != ZX_OK) {
            return status;
          }
          break;
        case tel_fake::kTerminateMsg:
          device_ptr->EventLoopCleanup();
          return 0;
        default:
          zxlogf(ERROR, "at-fake-transport: at_port undefined key %lu", packet.key);
          assert(0);
      }
    } else {
      zxlogf(ERROR, "at-fake-transport: at_port err %d", status);
      assert(0);
    }
  }
  return 0;
}

zx_status_t AtDevice::Bind() {
  // create a port to watch at messages
  zx_status_t status = zx::port::create(0, &GetCtrlChannelPort());
  if (status != ZX_OK) {
    zxlogf(ERROR, "at-fake-transport: failed to create a port: %s", zx_status_get_string(status));
    return status;
  }

  // create the handler thread
  GetCtrlThrd() = std::thread(at_fake_transport_thread, this);

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "at-fake";
  args.ctx = this;
  args.ops = &at_fake_device_ops;
  args.proto_id = ZX_PROTOCOL_AT_TRANSPORT;
  status = device_add(GetParentDevice(), &args, &GetTelDevPtr());
  if (status != ZX_OK) {
    zxlogf(ERROR, "at-fake-transport: could not add device: %d", status);
    zx_port_packet_t packet = {};
    packet.key = tel_fake::kTerminateMsg;
    GetCtrlChannelPort().queue(&packet);
    zxlogf(INFO, "at-fake-transport: joining thread");
    GetCtrlThrd().join();
    return status;
  }
  return status;
}

}  // namespace at_fake
