// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-device.h"
#include <ddktl/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <cstdio>
#include <future>
#include <thread>

#define QMI_INIT_REQ \
  { 1, 15, 0, 0, 0, 0, 0, 1, 34, 0, 4, 0, 1, 1, 0, 2 }
#define QMI_INIT_RESP \
  { 1, 23, 0, 128, 0, 0, 1, 1, 34, 0, 12, 0, 2, 4, 0, 0, 0, 0, 0, 1, 2, 0, 2, 1 }
#define QMI_IMEI_REQ \
  { 1, 12, 0, 0, 2, 1, 0, 1, 0, 37, 0, 0, 0 }
#define QMI_IMEI_RESP                                                                             \
  {                                                                                               \
    1, 41, 0, 128, 2, 1, 2, 1, 0, 37, 0, 29, 0, 2, 4, 0, 0, 0, 0, 0, 16, 1, 0, 48, 17, 15, 0, 51, \
        53, 57, 50, 54, 48, 48, 56, 48, 49, 54, 56, 51, 53, 49                                    \
  }
#define QMI_POWER_STA_REQ \
  { 1, 12, 0, 0, 2, 1, 0, 1, 0, 45, 0, 0, 0 }
#define QMI_PERIO_EVENT \
  { 1, 11, 0, 128, 0, 0, 2, 0, 39, 0, 0, 0 }
#define QMI_NONSENSE_RESP \
  { 1, 0 }
#define MIN(a, b) ((a) > (b) ? (b) : (a))

const uint8_t qmi_init_req[] = QMI_INIT_REQ;
const uint8_t qmi_imei_req[] = QMI_IMEI_REQ;
const uint8_t qmi_init_resp[] = QMI_INIT_RESP;
const uint8_t qmi_imei_resp[] = QMI_IMEI_RESP;
const uint8_t qmi_perio_event[] = QMI_PERIO_EVENT;
const uint8_t qmi_nonsense_resp[] = QMI_NONSENSE_RESP;

namespace fidl_qmi_transport = llcpp::fuchsia::hardware::telephony::transport;
namespace fidl_tel_snoop = ::llcpp::fuchsia::telephony::snoop;

namespace qmi_fake {

Device::Device(zx_device_t* device) : parent_(device) {}

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t qmi_fake_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .get_protocol = [](void* ctx, uint32_t proto_id, void* out_proto) -> zx_status_t {
      return DEV(ctx)->GetProtocol(proto_id, out_proto);
    },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) -> zx_status_t {
      return DEV(ctx)->DdkMessage(msg, txn);
    },
};
#undef DEV

void Device::SetChannel(::zx::channel transport,
                        fidl_qmi_transport::Qmi::Interface::SetChannelCompleter::Sync completer) {
  zx_status_t status = ZX_OK;
  zx_status_t set_channel_res = SetChannelToDevice(transport.release());
  fidl_qmi_transport::Qmi_SetChannel_Result result;
  if (set_channel_res == ZX_OK) {
    result.set_response(fidl_qmi_transport::Qmi_SetChannel_Response{});
  } else {
    result.set_err(set_channel_res);
  }
  completer.Reply(std::move(result));
  if (status != ZX_OK) {
    goto done;
  }
  if (set_channel_res == ZX_OK) {
    status = SetAsyncWait();
    if (status != ZX_OK) {
      CloseQmiChannel();
    }
  }
done:
  return;
}

void Device::SetNetwork(bool connected,
                        fidl_qmi_transport::Qmi::Interface::SetNetworkCompleter::Sync completer) {
  SetNetworkStatusToDevice(connected);
  completer.Reply();
}

void Device::SetSnoopChannel(
    ::zx::channel interface,
    fidl_qmi_transport::Qmi::Interface::SetSnoopChannelCompleter::Sync completer) {
  zx_status_t set_snoop_res = SetSnoopChannelToDevice(interface.release());
  fidl_qmi_transport::Qmi_SetSnoopChannel_Result result;
  if (set_snoop_res == ZX_OK) {
    result.set_response(fidl_qmi_transport::Qmi_SetSnoopChannel_Response{});
  } else {
    result.set_err(set_snoop_res);
  }
  completer.Reply(std::move(result));
}

zx_status_t Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fidl_qmi_transport::Qmi::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t Device::SetSnoopChannelToDevice(zx_handle_t channel) {
  zx_status_t result = ZX_OK;
  zx_port_packet_t packet;
  zx_status_t status;
  // Initialize a port to watch whether the other handle of snoop channel has
  // closed
  if (snoop_channel_port_ == ZX_HANDLE_INVALID) {
    status = zx_port_create(0, &snoop_channel_port_);
    if (status != ZX_OK) {
      zxlogf(ERROR,
             "qmi-fake: failed to create a port to watch snoop channel: "
             "%s\n",
             zx_status_get_string(status));
      return status;
    }
  } else {
    status = zx_port_wait(snoop_channel_port_, 0, &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "qmi-fake: timed out: %s\n", zx_status_get_string(status));
    } else if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
      zxlogf(INFO, "qmi-fake: snoop channel peer closed\n");
      snoop_channel_ = ZX_HANDLE_INVALID;
    }
  }

  if (snoop_channel_ != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "snoop channel already connected\n");
    result = ZX_ERR_ALREADY_BOUND;
  } else if (channel == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "get invalid snoop channel handle\n");
    result = ZX_ERR_BAD_HANDLE;
  } else {
    snoop_channel_ = channel;
    zx_object_wait_async(snoop_channel_, snoop_channel_port_, 0, ZX_CHANNEL_PEER_CLOSED,
                         ZX_WAIT_ASYNC_ONCE);
  }
  return result;
}

zx_status_t Device::CloseQmiChannel() {
  zx_status_t ret_val = ZX_OK;
  if (qmi_channel_ != ZX_HANDLE_INVALID) {
    ret_val = zx_handle_close(qmi_channel_);
    qmi_channel_ = ZX_HANDLE_INVALID;
  }
  return ret_val;
}

zx_handle_t Device::GetQmiChannel() { return qmi_channel_; }

zx_status_t Device::SetAsyncWait() {
  zx_status_t status =
      zx_object_wait_async(qmi_channel_, qmi_channel_port_, CHANNEL_MSG,
                           ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_WAIT_ASYNC_ONCE);
  return status;
}

static void sent_fake_qmi_msg(zx_handle_t channel, uint8_t* resp, uint32_t resp_size) {
  zx_status_t status;
  status = zx_channel_write(channel, 0, resp, resp_size, NULL, 0);
  if (status < 0) {
    zxlogf(ERROR, "qmi-fake-transport: failed to write message to channel: %s\n",
           zx_status_get_string(status));
  }
}

void Device::SnoopQmiMsg(uint8_t* snoop_data, uint32_t snoop_data_len,
                         fidl_tel_snoop::Direction direction) {
  if (snoop_channel_) {
    fidl_tel_snoop::Message snoop_msg;
    fidl_tel_snoop::QmiMessage qmi_msg;
    uint32_t current_length = sizeof(qmi_msg.opaque_bytes);
    qmi_msg.is_partial_copy = snoop_data_len > current_length;
    qmi_msg.direction = direction;
    qmi_msg.timestamp = zx_clock_get_monotonic();
    memcpy(qmi_msg.opaque_bytes.data_, snoop_data, current_length);
    snoop_msg.set_qmi_message(qmi_msg);
    fidl_tel_snoop::Publisher::Call::SendMessage_Deprecated(zx::unowned_channel(snoop_channel_),
                                                            std::move(snoop_msg));
  }
}

void Device::ReplyQmiMsg(uint8_t* req, uint32_t req_size, uint8_t* resp, uint32_t resp_size) {
  memset(resp, 170, resp_size);
  if (0 == memcmp(req, qmi_init_req, sizeof(qmi_init_req))) {
    memcpy(resp, qmi_perio_event, MIN(sizeof(qmi_perio_event), resp_size));
    SnoopQmiMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
    sent_fake_qmi_msg(qmi_channel_, resp, resp_size);
    memcpy(resp, qmi_init_resp, MIN(sizeof(qmi_init_resp), resp_size));
    SnoopQmiMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
    sent_fake_qmi_msg(qmi_channel_, resp, resp_size);
  } else if (0 == memcmp(req, qmi_imei_req, sizeof(qmi_imei_req))) {
    memcpy(resp, qmi_imei_resp, MIN(sizeof(qmi_imei_resp), resp_size));
    SnoopQmiMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
    sent_fake_qmi_msg(qmi_channel_, resp, resp_size);
    memcpy(resp, qmi_perio_event, MIN(sizeof(qmi_perio_event), resp_size));
    SnoopQmiMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
    sent_fake_qmi_msg(qmi_channel_, resp, resp_size);
  } else {
    zxlogf(ERROR, "qmi-fake-driver: unexpected qmi msg received\n");
    memcpy(resp, qmi_nonsense_resp, MIN(sizeof(qmi_nonsense_resp), resp_size));
    SnoopQmiMsg(resp, resp_size, fidl_tel_snoop::Direction::FROM_MODEM);
    sent_fake_qmi_msg(qmi_channel_, resp, resp_size);
  }
}

static int qmi_fake_transport_thread(void* cookie) {
  assert(cookie != NULL);
  Device* device_ptr = static_cast<Device*>(cookie);
  if (device_ptr->max_packet_size_ > 2048) {
    zxlogf(ERROR, "qmi-fake-transport: packet too big: %d\n", device_ptr->max_packet_size_);
    return ZX_ERR_IO_REFUSED;
  }
  uint8_t buffer[device_ptr->max_packet_size_];
  uint8_t repl_buffer[device_ptr->max_packet_size_];
  uint32_t length = sizeof(repl_buffer);
  uint32_t length_read = 0;
  zx_port_packet_t packet;
  zxlogf(INFO, "qmi-fake-transport: event loop initialized\n");
  while (true) {
    zx_status_t status = zx_port_wait(device_ptr->qmi_channel_port_, ZX_TIME_INFINITE, &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "qmi-fake-transport: timed out: %s\n", zx_status_get_string(status));
    } else {
      if (packet.key == CHANNEL_MSG) {
        if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
          zxlogf(ERROR, "qmi-fake-transport: channel closed\n");
          status = device_ptr->CloseQmiChannel();
          continue;
        }
        status = zx_channel_read(device_ptr->GetQmiChannel(), 0, buffer, NULL, sizeof(buffer), 0,
                                 &length_read, NULL);
        if (status != ZX_OK) {
          zxlogf(ERROR, "qmi-fake-transport: failed to read channel: %s\n",
                 zx_status_get_string(status));
          return status;
        }
        device_ptr->SnoopQmiMsg(buffer, sizeof(buffer), fidl_tel_snoop::Direction::TO_MODEM);
        // TODO (jiamingw): parse QMI msg, form reply and write back to channel.
        device_ptr->ReplyQmiMsg(buffer, length_read, repl_buffer, length);
        status = device_ptr->SetAsyncWait();
        if (status != ZX_OK) {
          return status;
        }
      }
    }
  }
  return 0;
}

zx_status_t Device::Bind() {
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "qmi-fake";
  args.ctx = this;
  args.ops = &qmi_fake_device_ops;
  args.proto_id = ZX_PROTOCOL_QMI_TRANSPORT;

  zx_status_t status = device_add(parent_, &args, &zxdev_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-fake: could not add device: %d\n", status);
    return status;
  }

  // create a port to watch qmi messages
  if (qmi_channel_port_ == ZX_HANDLE_INVALID) {
    status = zx_port_create(0, &qmi_channel_port_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "qmi-fake-transport: failed to create a port: %s\n",
             zx_status_get_string(status));
      return status;
    }
  }

  // create the handler thread
  int thread_result = thrd_create_with_name(&fake_qmi_thread_, qmi_fake_transport_thread,
                                            (void*)this, "qmi_fake_transport_thread");
  if (thread_result != thrd_success) {
    zxlogf(ERROR, "qmi-fake-transport: failed to create transport thread (%d)\n", thread_result);
    status = ZX_ERR_INTERNAL;
  }

  // set max_packet_size_ to maximum for now
  max_packet_size_ = 0x7FF;
  return status;
}

void Device::Release() { delete this; }

void Device::Unbind() { device_remove(zxdev_); }

zx_status_t Device::GetProtocol(uint32_t proto_id, void* out_proto) {
  if (proto_id != ZX_PROTOCOL_QMI_TRANSPORT) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t Device::SetChannelToDevice(zx_handle_t transport) {
  zx_status_t result = ZX_OK;
  if (qmi_channel_ != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "qmi-fake: already bound, failing\n");
    result = ZX_ERR_ALREADY_BOUND;
  } else if (transport == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "qmi-fake: invalid channel handle\n");
    result = ZX_ERR_BAD_HANDLE;
  } else {
    qmi_channel_ = transport;
  }
  return result;
}

zx_status_t Device::SetNetworkStatusToDevice(bool connected) {
  connected_ = connected;
  return ZX_OK;
}

}  // namespace qmi_fake
