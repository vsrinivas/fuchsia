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

#include <ddktl/fidl.h>

namespace fidl_qmi_transport = llcpp::fuchsia::hardware::telephony::transport;
namespace fidl_tel_snoop = ::llcpp::fuchsia::telephony::snoop;

namespace qmi_fake {
constexpr uint8_t kQmiInitReq[] = {1, 15, 0, 0, 0, 0, 0, 1, 34, 0, 4, 0, 1, 1, 0, 2};
constexpr uint8_t kQmiImeiReq[] = {1, 12, 0, 0, 2, 1, 0, 1, 0, 37, 0, 0, 0};
constexpr uint8_t kQmiInitResp[] = {1, 23, 0, 128, 0, 0, 1, 1, 34, 0, 12, 0,
                                    2, 4,  0, 0,   0, 0, 0, 1, 2,  0, 2,  1};
constexpr uint8_t kQmiImeiResp[] = {1,  41, 0,  128, 2,  1,  2,  1,  0,  37, 0,  29, 0,  2,
                                    4,  0,  0,  0,   0,  0,  16, 1,  0,  48, 17, 15, 0,  51,
                                    53, 57, 50, 54,  48, 48, 56, 48, 49, 54, 56, 51, 53, 49};
constexpr uint8_t kQmiPerioEvent[] = {1, 11, 0, 128, 0, 0, 2, 0, 39, 0, 0, 0};
constexpr uint8_t kQmiNonsenseResp[] = {1, 0};

constexpr uint32_t kTelCtrlPlanePktMax = 2048;

QmiDevice::QmiDevice(zx_device_t* device) : Device(device) {}

#define DEV(c) static_cast<QmiDevice*>(c)
static zx_protocol_device_t qmi_fake_device_ops = {
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

static void sent_fake_qmi_msg(zx::channel& channel, uint8_t* resp, uint32_t resp_size) {
  zx_status_t status;
  status = channel.write(0, resp, resp_size, NULL, 0);
  if (status < 0) {
    zxlogf(ERROR, "qmi-fake-transport: failed to write message to channel: %s",
           zx_status_get_string(status));
  }
}

void QmiDevice::SnoopCtrlMsg(uint8_t* snoop_data, uint32_t snoop_data_len,
                             fidl_tel_snoop::Direction direction) {
  if (GetCtrlSnoopChannel()) {
    fidl_tel_snoop::Message snoop_msg;
    fidl_tel_snoop::QmiMessage qmi_msg;
    uint32_t current_length =
        std::min(static_cast<std::size_t>(snoop_data_len), sizeof(qmi_msg.opaque_bytes));
    qmi_msg.is_partial_copy = snoop_data_len > current_length;
    qmi_msg.direction = direction;
    qmi_msg.timestamp = zx_clock_get_monotonic();
    memcpy(qmi_msg.opaque_bytes.data_, snoop_data, current_length);
    snoop_msg.set_qmi_message(fidl::unowned_ptr(&qmi_msg));
    zxlogf(INFO, "qmi-fake-transport: snoop msg %u %u %u %u sent", qmi_msg.opaque_bytes.data_[0],
           qmi_msg.opaque_bytes.data_[1], qmi_msg.opaque_bytes.data_[2],
           qmi_msg.opaque_bytes.data_[3]);
    fidl_tel_snoop::Publisher::Call::SendMessage(zx::unowned_channel(GetCtrlSnoopChannel().get()),
                                                 std::move(snoop_msg));
  }
}

void QmiDevice::ReplyCtrlMsg(uint8_t* req, uint32_t req_size, uint8_t* resp, uint32_t resp_size) {
  memset(resp, 170, resp_size);
  if (0 == memcmp(req, kQmiInitReq, sizeof(kQmiInitReq))) {
    memcpy(resp, kQmiPerioEvent,
           std::min(sizeof(kQmiPerioEvent), static_cast<std::size_t>(resp_size)));
    sent_fake_qmi_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
    memcpy(resp, kQmiInitResp, std::min(sizeof(kQmiInitResp), static_cast<std::size_t>(resp_size)));
    sent_fake_qmi_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
  } else if (0 == memcmp(req, kQmiImeiReq, sizeof(kQmiImeiReq))) {
    memcpy(resp, kQmiImeiResp, std::min(sizeof(kQmiImeiResp), static_cast<std::size_t>(resp_size)));
    sent_fake_qmi_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
    memcpy(resp, kQmiPerioEvent,
           std::min(sizeof(kQmiPerioEvent), static_cast<std::size_t>(resp_size)));
    sent_fake_qmi_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
  } else {
    zxlogf(ERROR, "qmi-fake-driver: unexpected qmi msg received");
    memcpy(resp, kQmiNonsenseResp,
           std::min(sizeof(kQmiNonsenseResp), static_cast<std::size_t>(resp_size)));
    sent_fake_qmi_msg(GetCtrlChannel(), resp, resp_size);
    SnoopCtrlMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
  }
}

static int qmi_fake_transport_thread(void* cookie) {
  assert(cookie != NULL);
  QmiDevice* device_ptr = static_cast<QmiDevice*>(cookie);
  uint32_t req_len = 0;
  uint8_t req_buf[kTelCtrlPlanePktMax];
  uint8_t resp_buf[kTelCtrlPlanePktMax];

  zx_port_packet_t packet;
  zxlogf(INFO, "qmi-fake-transport: event loop initialized");
  while (true) {
    zx_status_t status = device_ptr->GetCtrlChannelPort().wait(zx::time::infinite(), &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "qmi-fake-transport: timed out: %s", zx_status_get_string(status));
    } else if (status == ZX_OK) {
      switch (packet.key) {
        case tel_fake::kChannelMsg:
          if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
            zxlogf(ERROR, "qmi-fake-transport: channel closed");
            status = device_ptr->CloseCtrlChannel();
            continue;
          }
          status = device_ptr->GetCtrlChannel().read(0, req_buf, NULL, kTelCtrlPlanePktMax, 0,
                                                     &req_len, NULL);
          if (status != ZX_OK) {
            zxlogf(ERROR, "qmi-fake-transport: failed to read channel: %s",
                   zx_status_get_string(status));
            return status;
          }
          device_ptr->SnoopCtrlMsg(req_buf, kTelCtrlPlanePktMax,
                                   fidl_tel_snoop::Direction::TO_MODEM);
          // TODO (jiamingw): parse QMI msg, form reply and write back to channel.
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
          zxlogf(ERROR, "qmi-fake-transport: qmi_port undefined key %lu", packet.key);
          assert(0);
      }
    } else {
      zxlogf(ERROR, "qmi-fake-transport: qmi_port err %d", status);
      assert(0);
    }
  }
  return 0;
}

zx_status_t QmiDevice::Bind() {
  // create a port to watch qmi messages
  zx_status_t status = zx::port::create(0, &GetCtrlChannelPort());
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-fake-transport: failed to create a port: %s", zx_status_get_string(status));
    return status;
  }

  // create the handler thread
  GetCtrlThrd() = std::thread(qmi_fake_transport_thread, this);

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "qmi-fake";
  args.ctx = this;
  args.ops = &qmi_fake_device_ops;
  args.proto_id = ZX_PROTOCOL_QMI_TRANSPORT;
  status = device_add(GetParentDevice(), &args, &GetTelDevPtr());
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-fake-transport: could not add device: %d", status);
    zx_port_packet_t packet = {};
    packet.key = tel_fake::kTerminateMsg;
    GetCtrlChannelPort().queue(&packet);
    zxlogf(INFO, "qmi-fake-transport: joining thread");
    GetCtrlThrd().join();
    return status;
  }
  return status;
}

}  // namespace qmi_fake
