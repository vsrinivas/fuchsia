// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qmi-usb-transport.h"

#include <lib/fidl/llcpp/coding.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <future>
#include <thread>

#include <ddk/debug.h>
#include <ddktl/fidl.h>

#ifndef _ALL_SOURCE
#define _ALL_SOURCE
#endif
#include <threads.h>

#define ETHERNET_MAX_TRANSMIT_DELAY 100
#define ETHERNET_MAX_RECV_DELAY 100
#define ETHERNET_TRANSMIT_DELAY 10
#define ETHERNET_RECV_DELAY 10
#define ETHERNET_INITIAL_TRANSMIT_DELAY 0
#define ETHERNET_INITIAL_RECV_DELAY 0

#define ETHERNET_FRAME_OFFSET 14

namespace telephony_transport = ::llcpp::fuchsia::hardware::telephony::transport;
namespace telephony_snoop = ::llcpp::fuchsia::telephony::snoop;

// TODO (jiamingw): investigate whether it can be replaced by eth::Operation
typedef struct txn_info {
  ethernet_netbuf_t netbuf;
  ethernet_impl_queue_tx_callback completion_cb;
  void* cookie;
  list_node_t node;
} txn_info_t;

static void complete_txn(txn_info_t* txn, zx_status_t status) {
  txn->completion_cb(txn->cookie, status, &txn->netbuf);
}

namespace qmi_usb {
constexpr uint32_t kMaxTxBufSz = 32768;
constexpr uint32_t kMaxRxBufSz = 32768;

static void usb_write_complete(void* ctx, usb_request_t* request);

zx_status_t Device::SetChannelToDevice(zx_handle_t channel) {
  zxlogf(INFO, "qmi-usb-transport: getting channel from transport\n");
  zx_status_t result = ZX_OK;

  if (qmi_channel_ != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "qmi-usb-transport: already bound, failing\n");
    result = ZX_ERR_ALREADY_BOUND;
  } else if (channel == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "qmi-usb-transport: invalid channel handle\n");
    result = ZX_ERR_BAD_HANDLE;
  } else {
    qmi_channel_ = channel;
  }
  return result;
}

zx_status_t Device::QueueRequest(const uint8_t* data, size_t length, usb_request_t* req) {
  req->header.length = length;
  if (length < 41) {
    zxlogf(ERROR, "qmi-usb-transport: length is too short (length: %zx) \n", length);
    return ZX_ERR_IO;
  }

  if (length > 1024) {
    zxlogf(ERROR,
           "qmi-usb-transport: length is greater than buffer size. (length: "
           "%zx) \n",
           length);
    return ZX_ERR_IO;
  }

  // Check if this in an arp frame, short-circuit return a synthetic one if so
  const uint8_t* eth = &data[ETHERNET_FRAME_OFFSET];
  if ((eth[0] == 0x00) &&                      // hardware type
      (eth[1] == 0x01) && (eth[2] == 0x08) &&  // protocol
      (eth[3] == 0x00) && (eth[4] == 0x06) &&  // hardware len
      (eth[5] == 0x04) &&                      // protocol len
      (eth[6] == 0x00) &&                      // arp request op
      (eth[7] == 0x01)) {
    uint8_t read_data[] = {
        // ethernet frame
        0x62, 0x77, 0x62, 0x62, 0x77, 0x62,  // destination mac addr (bwb)
        0x79, 0x61, 0x6B, 0x79, 0x61, 0x6B,  // source mac addr (yak)
        0x08, 0x06,                          // arp ethertype
        // data payload
        0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x02,  // ARP header
        0x79, 0x61, 0x6B, 0x79, 0x61, 0x6B,              // yak mac addr
        eth[24], eth[25], eth[26], eth[27],              // swapped IP addr
        0x62, 0x77, 0x62, 0x62, 0x77, 0x62,              // bwb mac addr
        eth[14], eth[15], eth[16], eth[17],              // swapped IP addr
    };

    zx_nanosleep(zx_deadline_after(ZX_USEC(tx_endpoint_delay_)));
    mtx_lock(&ethernet_mutex_);
    if (ethernet_ifc_.ops) {
      ethernet_ifc_recv(&ethernet_ifc_, read_data, sizeof(read_data), 0);
    }
    mtx_unlock(&ethernet_mutex_);
    return ZX_OK;
  }

  ssize_t ip_length = ((eth[2] << 8) + eth[3]);
  if (ip_length > (ssize_t)length) {
    zxlogf(ERROR,
           "qmi-usb-transport: length of IP packet is more than the ethernet "
           "frame! %zx/%zd \n",
           ip_length, length);
    return ZX_ERR_IO;
  }

  ssize_t bytes_copied = usb_request_copy_to(req, eth, ip_length, 0);
  if (bytes_copied < 0) {
    zxlogf(ERROR, "qmi-usb-transport: failed to copy data into send txn (error %zd)\n",
           bytes_copied);
    return ZX_ERR_IO;
  }

  usb_request_complete_t complete = {
      .callback = usb_write_complete,
      .ctx = this,
  };
  usb_request_queue(&usb_, req, &complete);
  return ZX_OK;
}

zx_status_t inline Device::SendLocked(ethernet_netbuf_t* netbuf) {
  const uint8_t* byte_data = (const uint8_t*)netbuf->data_buffer;
  size_t length = netbuf->data_size;

  // Make sure that we can get all of the tx buffers we need to use
  usb_request_t* tx_req = usb_req_list_remove_head(&tx_txn_bufs_, parent_req_size_);
  if (tx_req == nullptr) {
    return ZX_ERR_SHOULD_WAIT;
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(tx_endpoint_delay_)));
  zx_status_t status;
  if ((status = QueueRequest(byte_data, length, tx_req)) != ZX_OK) {
    zx_status_t add_status = usb_req_list_add_tail(&tx_txn_bufs_, tx_req, parent_req_size_);
    ZX_DEBUG_ASSERT(add_status == ZX_OK);
    return status;
  }

  return ZX_OK;
}

void Device::QmiUpdateOnlineStatus(bool is_online) {
  mtx_lock(&ethernet_mutex_);
  if ((is_online && online_) || (!is_online && !online_)) {
    mtx_unlock(&ethernet_mutex_);
    return;
  }

  if (is_online) {
    zxlogf(INFO, "qmi-usb-transport: connected to network\n");
    online_ = true;
    if (ethernet_ifc_.ops) {
      ethernet_ifc_status(&ethernet_ifc_, online_ ? ETHERNET_STATUS_ONLINE : 0);
    } else {
      zxlogf(ERROR, "qmi-usb-transport: not connected to ethermac interface\n");
    }
  } else {
    zxlogf(INFO, "qmi-usb-transport: no connection to network\n");
    online_ = false;
    if (ethernet_ifc_.ops) {
      ethernet_ifc_status(&ethernet_ifc_, 0);
    }
  }
  mtx_unlock(&ethernet_mutex_);
}

zx_status_t Device::SetAsyncWait() {
  return zx_object_wait_async(qmi_channel_, qmi_channel_port_, CHANNEL_MSG,
                              ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_WAIT_ASYNC_ONCE);
}

zx_status_t Device::SetSnoopChannelToDevice(zx_handle_t channel) {
  zx_port_packet_t packet;
  zx_status_t status = ZX_OK;
  // Initialize a port to watch whether the other handle of snoop channel has
  // closed
  if (snoop_channel_port_ == ZX_HANDLE_INVALID) {
    status = zx_port_create(0, &snoop_channel_port_);
    if (status != ZX_OK) {
      zxlogf(ERROR,
             "qmi-usb-transport: failed to create a port to watch snoop channel: "
             "%s\n",
             zx_status_get_string(status));
      return status;
    }
  } else {
    status = zx_port_wait(snoop_channel_port_, 0, &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "qmi-usb-transport: timed out: %s\n", zx_status_get_string(status));
    } else if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
      zxlogf(INFO, "qmi-usb-transport: snoop channel peer closed\n");
      snoop_channel_ = ZX_HANDLE_INVALID;
    }
  }

  status = ZX_OK;
  if (snoop_channel_ != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "snoop channel already connected\n");
    status = ZX_ERR_ALREADY_BOUND;
  } else if (channel == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "get invalid snoop channel handle\n");
    status = ZX_ERR_BAD_HANDLE;
  } else {
    snoop_channel_ = channel;
    zx_object_wait_async(snoop_channel_, snoop_channel_port_, 0, ZX_CHANNEL_PEER_CLOSED,
                        ZX_WAIT_ASYNC_ONCE);
  }
  return status;
}

zx_status_t Device::CloseQmiChannel() {
  zx_status_t ret_val = zx_handle_close(qmi_channel_);
  qmi_channel_ = ZX_HANDLE_INVALID;
  return ret_val;
}

void Device::SetChannel(::zx::channel transport, SetChannelCompleter::Sync completer) {
  zx_status_t set_channel_res = SetChannelToDevice(transport.release());
  if (set_channel_res == ZX_OK) {
    telephony_transport::Qmi_SetChannel_Response response{
        .__reserved = 0,
    };
    completer.Reply(telephony_transport::Qmi_SetChannel_Result::WithResponse(&response));
    zx_status_t status = SetAsyncWait();
    if (status != ZX_OK) {
      CloseQmiChannel();
    }
  } else {
    completer.Reply(
        telephony_transport::Qmi_SetChannel_Result::WithErr(static_cast<int32_t*>(&set_channel_res)));
  }
}

void Device::SetNetwork(bool connected, SetNetworkCompleter::Sync completer) {
  QmiUpdateOnlineStatus(connected);
  completer.Reply();
}

void Device::SetSnoopChannel(::zx::channel interface, SetSnoopChannelCompleter::Sync completer) {
  zx_status_t set_snoop_res = SetSnoopChannelToDevice(interface.release());
  if (set_snoop_res == ZX_OK) {
    telephony_transport::Qmi_SetSnoopChannel_Response response{
        .__reserved = 0,
    };
    completer.Reply(telephony_transport::Qmi_SetSnoopChannel_Result::WithResponse(&response));
  } else {
    completer.Reply(telephony_transport::Qmi_SetSnoopChannel_Result::WithErr(
        static_cast<int32_t*>(&set_snoop_res)));
  }
}

zx_status_t Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  telephony_transport::Qmi::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void Device::Release() { delete this; }

void Device::DdkRelease() {
  zxlogf(INFO, "qmi-usb-transport: releasing device\n");
  Release();
}

zx_status_t Device::Unbind() { return DdkRemoveDeprecated(); }

void Device::DdkUnbindNew(ddk::UnbindTxn txn) {
  zx_status_t result = Unbind();
  if (result != ZX_OK) {
    zxlogf(ERROR, "Failed to unbind qmi-usb-transport driver. Cannot remove device: %u\n", result);
  }
  txn.Reply();
}

uint32_t Device::GetMacAddr(uint8_t* buffer, uint32_t buffer_length) {
  if (buffer == nullptr) {
    return 0;
  }
  uint32_t copy_length = std::min((uint32_t)sizeof(mac_addr_), buffer_length);
  memcpy(buffer, mac_addr_, copy_length);
  return copy_length;
}

static zx_status_t qmi_ethernet_impl_query(void* ctx, uint32_t options, ethernet_info_t* info) {
  Device* device = static_cast<Device*>(ctx);
  zxlogf(INFO, "qmi-usb-transport: %s called\n", __FUNCTION__);
  // No options are supported
  if (options) {
    zxlogf(ERROR, "qmi-usb-transport: unexpected options to ethernet_query\n");

    return ZX_ERR_INVALID_ARGS;
  }
  memset(info, 0, sizeof(*info));
  info->mtu = 1024;

  device->GetMacAddr(info->mac, sizeof(info->mac));
  info->netbuf_size = sizeof(txn_info_t);
  return ZX_OK;
}
zx_status_t Device::QmiEthernetStartHandler(const ethernet_ifc_protocol_t* ifc) {
  zx_status_t status = ZX_OK;
  mtx_lock(&ethernet_mutex_);
  if (ethernet_ifc_.ops) {
    status = ZX_ERR_ALREADY_BOUND;
  } else {
    ethernet_ifc_ = *ifc;
    ethernet_ifc_status(&ethernet_ifc_, online_ ? ETHERNET_STATUS_ONLINE : 0);
  }
  mtx_unlock(&ethernet_mutex_);
  return status;
}
static zx_status_t qmi_ethernet_impl_start(void* ctx, const ethernet_ifc_protocol_t* ifc) {
  zxlogf(INFO, "qmi-usb-transport: %s called\n", __FUNCTION__);
  Device* device_ptr = static_cast<Device*>(ctx);
  return device_ptr->QmiEthernetStartHandler(ifc);
}
static zx_status_t qmi_ethernet_impl_set_param(void* cookie, uint32_t param, int32_t value,
                                               const void* data, size_t data_size) {
  return ZX_ERR_NOT_SUPPORTED;
}
void Device::QmiEthernetStopHandler() {
  mtx_lock(&ethernet_mutex_);
  ethernet_ifc_.ops = nullptr;
  mtx_unlock(&ethernet_mutex_);
}

static void qmi_ethernet_impl_stop(void* ctx) {
  zxlogf(INFO, "qmi-usb-transport: %s called\n", __FUNCTION__);
  Device* device_ptr = static_cast<Device*>(ctx);
  device_ptr->QmiEthernetStopHandler();
}

void Device::QmiEthernetQueueTxHandler(uint32_t options, ethernet_netbuf_t* netbuf,
                                       ethernet_impl_queue_tx_callback completion_cb,
                                       void* cookie) {
  size_t length = netbuf->data_size;
  zx_status_t status = ZX_OK;
  txn_info_t* txn = containerof(netbuf, txn_info_t, netbuf);
  txn->completion_cb = completion_cb;
  txn->cookie = cookie;
  // TODO mtu better
  if (length > 1024 || length == 0) {
    complete_txn(txn, ZX_ERR_INVALID_ARGS);
    return;
  }
  mtx_lock(&tx_mutex_);
  if (device_unbound_) {
    status = ZX_ERR_IO_NOT_PRESENT;
  } else {
    status = SendLocked(netbuf);
    if (status == ZX_ERR_SHOULD_WAIT) {
      // No buffers available, queue it up
      list_add_tail(&tx_pending_infos_, &txn->node);
    }
  }
  mtx_unlock(&tx_mutex_);
  if (status != ZX_ERR_SHOULD_WAIT) {
    complete_txn(txn, status);
  }
  return;
}
static void qmi_ethernet_impl_queue_tx(void* context, uint32_t options, ethernet_netbuf_t* netbuf,
                                       ethernet_impl_queue_tx_callback completion_cb,
                                       void* cookie) {
  Device* device_ptr = static_cast<Device*>(context);
  device_ptr->QmiEthernetQueueTxHandler(options, netbuf, completion_cb, cookie);
}
static ethernet_impl_protocol_ops_t ethernet_impl_ops = {
    .query = qmi_ethernet_impl_query,
    .stop = qmi_ethernet_impl_stop,
    .start = qmi_ethernet_impl_start,
    .queue_tx = qmi_ethernet_impl_queue_tx,
    .set_param = qmi_ethernet_impl_set_param,
};

static zx_protocol_device_t eth_qmi_ops = {
    .version = DEVICE_OPS_VERSION,
};

void Device::QmiInterruptHandler(usb_request_t* request) {
  if (request->response.actual < sizeof(usb_cdc_notification_t)) {
    zxlogf(ERROR, "qmi-usb-transport: ignored interrupt (size = %ld)\n",
           (long)request->response.actual);
    return;
  }

  usb_cdc_notification_t usb_req;
  usb_request_copy_from(request, &usb_req, sizeof(usb_cdc_notification_t), 0);

  uint16_t packet_size = max_packet_size_;
  if (packet_size > 2048) {
    zxlogf(ERROR, "qmi-usb-transport: packet too big: %d\n", packet_size);
    return;
  }

  switch (usb_req.bNotification) {
    case USB_CDC_NC_RESPONSE_AVAILABLE:
      UsbCdcIntHander(packet_size);
      break;
    default:
      zxlogf(ERROR, "qmi-usb-transport: Unknown Notification Type for QMI: %d\n",
             usb_req.bNotification);
      break;
  }
}

void Device::UsbCdcIntHander(uint16_t packet_size) {
  zx_status_t status;
  uint8_t buffer[packet_size];

  status = usb_control_in(&usb_, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                          USB_CDC_GET_ENCAPSULATED_RESPONSE, 0, QMI_INTERFACE_NUM, ZX_TIME_INFINITE,
                          buffer, packet_size, nullptr);
  if (!qmi_channel_) {
    zxlogf(WARN, "qmi-usb-transport: recieving USB CDC frames without a channel\n");
    return;
  }
  status = zx_channel_write(qmi_channel_, 0, buffer, sizeof(buffer), nullptr, 0);
  if (status < 0) {
    zxlogf(ERROR, "qmi-usb-transport: failed to write message to channel: %s\n",
           zx_status_get_string(status));
  }
  if (snoop_channel_) {
    SnoopQmiMsgSend(buffer, sizeof(buffer), telephony_snoop::Direction::FROM_MODEM);
  }
  return;
}

zx_handle_t Device::GetQmiChannelPort() { return qmi_channel_port_; }

void Device::SnoopQmiMsgSend(uint8_t* msg_arr, uint32_t msg_arr_len,
                             telephony_snoop::Direction direction) {
  telephony_snoop::QmiMessage qmi_msg;
  uint32_t current_length = std::min(msg_arr_len, (uint32_t)sizeof(qmi_msg.opaque_bytes));
  qmi_msg.is_partial_copy = true;  // do not know the real length of QMI message for now
  qmi_msg.direction = direction;
  qmi_msg.timestamp = zx_clock_get_monotonic();
  memcpy(qmi_msg.opaque_bytes.data_, msg_arr, current_length);
  telephony_snoop::Message snoop_msg = telephony_snoop::Message::WithQmiMessage(&qmi_msg);
  telephony_snoop::Publisher::Call::SendMessage(zx::unowned_channel(snoop_channel_),
                                                std::move(snoop_msg));
}

static void qmi_interrupt_cb(void* ctx, usb_request_t* req) {
  Device* device = static_cast<Device*>(ctx);

  zx_port_packet_t packet = {};
  packet.key = INTERRUPT_MSG;
  zx_port_queue(device->GetQmiChannelPort(), &packet);
}

int Device::EventLoop(void) {
  usb_request_t* txn = int_txn_buf_;
  usb_request_complete_t complete = {
      .callback = qmi_usb::qmi_interrupt_cb,
      .ctx = this,
  };
  usb_request_queue(&usb_, txn, &complete);
  zxlogf(INFO, "successfully queued int req\n");
  if (max_packet_size_ > 2048) {
    zxlogf(ERROR, "qmi-usb-transport: packet too big: %d\n", max_packet_size_);
    return ZX_ERR_IO_REFUSED;
  }
  uint8_t buffer[max_packet_size_];
  uint32_t length = sizeof(buffer);
  zx_port_packet_t packet;
  while (true) {
    zx_status_t status = zx_port_wait(qmi_channel_port_, ZX_TIME_INFINITE, &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "qmi-usb-transport: timed out: %s\n", zx_status_get_string(status));
      continue;
    }
    if (packet.key == CHANNEL_MSG) {
      if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
        zxlogf(INFO, "qmi-usb-transport: channel closed\n");
        status = CloseQmiChannel();
        if (status != ZX_OK) {
          zxlogf(ERROR, "qmi-usb-transport: failed to close QMI channel: %s\n",
                 zx_status_get_string(status));
        }
        continue;
      }
      status =
          zx_channel_read(qmi_channel_, 0, buffer, nullptr, sizeof(buffer), 0, &length, nullptr);
      if (status != ZX_OK) {
        zxlogf(ERROR, "qmi-usb-transport: failed to read channel: %s\n",
               zx_status_get_string(status));
        return status;
      }
      status = usb_control_out(&usb_, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                               USB_CDC_SEND_ENCAPSULATED_COMMAND, 0, 8, ZX_TIME_INFINITE, buffer,
                               length);
      if (status != ZX_OK) {
        zxlogf(ERROR, "qmi-usb-transport: got an bad status from usb_control_out: %s\n",
               zx_status_get_string(status));
        return status;
      }
      status = SetAsyncWait();
      if (status != ZX_OK) {
        return status;
      }
      if (snoop_channel_) {
        SnoopQmiMsgSend(buffer, sizeof(buffer), telephony_snoop::Direction::TO_MODEM);
      }
    } else if (packet.key == INTERRUPT_MSG) {
      if (txn->response.status == ZX_OK) {
        QmiInterruptHandler(txn);
        usb_request_complete_t complete = {
            .callback = qmi_interrupt_cb,
            .ctx = this,
        };
        usb_request_queue(&usb_, txn, &complete);
      } else if (txn->response.status == ZX_ERR_PEER_CLOSED ||
                 txn->response.status == ZX_ERR_IO_NOT_PRESENT) {
        zxlogf(INFO, "qmi-usb-transport: terminating interrupt handling thread\n");
        return txn->response.status;
      }
    } else {
      zxlogf(ERROR, "qmi-usb-transport: invalid pkt key\n");
    }
  }  // while(true)
}

static int qmi_transport_thread(void* ctx) {
  Device* device_ptr = static_cast<Device*>(ctx);
  return device_ptr->EventLoop();
}

// Note: the assumption made here is that no rx transmissions will be processed
// in parallel, so we do not maintain an rx mutex.
void Device::UsbRecv(usb_request_t* request) {
  size_t len = request->response.actual;

  uint8_t* read_data;
  zx_status_t status = usb_request_mmap(request, reinterpret_cast<void**>(&read_data));
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: usb_request_mmap failed with status %d\n", status);
    return;
  }

  if (len > 2048) {
    zxlogf(ERROR, "qmi-usb-transport: recieved usb packet is too large: %zd\n", len);
    return;
  }

  uint8_t send_data[len + ETHERNET_FRAME_OFFSET];  // woo! VLA!
  send_data[0] = 0x62;                             // destination mac addr
  send_data[1] = 0x77;
  send_data[2] = 0x62;
  send_data[3] = 0x62;
  send_data[4] = 0x77;
  send_data[5] = 0x62;

  send_data[6] = 0x79;  // source mac addr
  send_data[7] = 0x61;
  send_data[8] = 0x6B;
  send_data[9] = 0x79;
  send_data[10] = 0x61;
  send_data[11] = 0x6B;

  send_data[12] = 0x08;
  send_data[13] = 0x00;

  memcpy(&send_data[ETHERNET_FRAME_OFFSET], read_data, len);
  mtx_lock(&ethernet_mutex_);
  if (ethernet_ifc_.ops) {
    ethernet_ifc_recv(&ethernet_ifc_, send_data, len + ETHERNET_FRAME_OFFSET, 0);
  }
  mtx_unlock(&ethernet_mutex_);
}

static void usb_read_complete(void* ctx, usb_request_t* request) {
  Device* device_ptr = static_cast<Device*>(ctx);
  device_ptr->UsbReadCompleteHandler(request);
}

void Device::UsbReadCompleteHandler(usb_request_t* request) {
  if (request->response.status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: usb_read_complete called with status %d\n",
           (int)request->response.status);
  }

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(ERROR, "qmi-usb-transport: resetting receive endpoint\n");
    usb_reset_endpoint(&usb_, rx_endpoint_addr_);
  } else if (request->response.status == ZX_ERR_IO_INVALID) {
    if (rx_endpoint_delay_ < ETHERNET_MAX_RECV_DELAY) {
      rx_endpoint_delay_ += ETHERNET_RECV_DELAY;
    }
    zxlogf(ERROR,
           "qmi-usb-transport: slowing down the requests by %d usec."
           "Resetting the recv endpoint\n",
           ETHERNET_RECV_DELAY);
    usb_reset_endpoint(&usb_, rx_endpoint_addr_);
  } else if (request->response.status == ZX_OK) {
    UsbRecv(request);
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(rx_endpoint_delay_)));

  usb_request_complete_t complete = {
      .callback = qmi_usb::usb_read_complete,
      .ctx = this,
  };
  usb_request_queue(&usb_, request, &complete);
}

void Device::UsbWriteCompleteHandler(usb_request_t* request) {
  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  // Return transmission buffer to pool
  mtx_lock(&tx_mutex_);
  zx_status_t status = usb_req_list_add_tail(&tx_txn_bufs_, request, parent_req_size_);
  ZX_DEBUG_ASSERT(status == ZX_OK);

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(ERROR, "qmi-usb-transport: resetting transmit endpoint\n");
    usb_reset_endpoint(&usb_, tx_endpoint_addr_);
  }

  if (request->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(ERROR,
           "qmi-usb-transport: slowing down the requests by %d usec."
           "Resetting the transmit endpoint\n",
           ETHERNET_TRANSMIT_DELAY);
    if (tx_endpoint_delay_ < ETHERNET_MAX_TRANSMIT_DELAY) {
      tx_endpoint_delay_ += ETHERNET_TRANSMIT_DELAY;
    }
    usb_reset_endpoint(&usb_, tx_endpoint_addr_);
  }

  bool additional_tx_queued = false;
  txn_info_t* txn;
  zx_status_t send_status = ZX_OK;
  if (!list_is_empty(&tx_pending_infos_)) {
    txn = list_peek_head_type(&tx_pending_infos_, txn_info_t, node);
    if ((send_status = SendLocked(&txn->netbuf)) != ZX_ERR_SHOULD_WAIT) {
      list_remove_head(&tx_pending_infos_);
      additional_tx_queued = true;
    }
  }

  mtx_unlock(&tx_mutex_);

  mtx_lock(&ethernet_mutex_);
  if (additional_tx_queued) {
    complete_txn(txn, send_status);
  }
  mtx_unlock(&ethernet_mutex_);

  // When the interface is offline, the transaction will complete with status
  // set to ZX_ERR_IO_NOT_PRESENT. There's not much we can do except ignore it.
}

static void usb_write_complete(void* ctx, usb_request_t* request) {
  Device* device_ptr = static_cast<Device*>(ctx);
  device_ptr->UsbWriteCompleteHandler(request);
}

Device::Device(zx_device_t* parent)
    : ddk::Device<Device, ddk::UnbindableNew, ddk::Messageable>(parent) {
  usb_device_ = parent;
}

void Device::QmiBindFailedNoErr(usb_request_t* int_buf) {
  if (int_buf) {
    usb_request_release(int_buf);
  }
}

void Device::QmiBindFailedErr(zx_status_t status, usb_request_t* int_buf) {
  zxlogf(ERROR, "qmi-usb-transport: bind failed: %s\n", zx_status_get_string(status));
  QmiBindFailedNoErr(int_buf);
}

zx_status_t Device::Bind() {
  zx_status_t status;
  usb_request_t* int_buf = nullptr;

  // Set up USB stuff
  usb_protocol_t usb;
  status = device_get_protocol(usb_device_, ZX_PROTOCOL_USB, &usb);
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: get protocol failed: %s\n", zx_status_get_string(status));
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  // Initialize context
  memcpy(&usb_, &usb, sizeof(usb_));
  list_initialize(&tx_txn_bufs_);
  list_initialize(&tx_pending_infos_);
  mtx_init(&ethernet_mutex_, mtx_plain);
  mtx_init(&tx_mutex_, mtx_plain);

  parent_req_size_ = usb_get_request_size(&usb_);
  uint64_t req_size = parent_req_size_ + sizeof(usb_req_internal_t);
  ZX_DEBUG_ASSERT(parent_req_size_ != 0);

  // find our endpoints
  usb_desc_iter_t iter;
  zx_status_t result = usb_desc_iter_init(&usb, &iter);
  if (result < 0) {
    status = result;
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  // QMI needs to bind to interface QMI_INTERFACE_NUM on current hardware.
  // Ignore the others for now.
  // TODO generic way of describing usb interfaces
  usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
  if (!intf || intf->bInterfaceNumber != QMI_INTERFACE_NUM) {
    usb_desc_iter_release(&iter);
    status = ZX_ERR_NOT_SUPPORTED;
    QmiBindFailedNoErr(int_buf);
    return status;
  }

  if (intf->bNumEndpoints != 3) {
    zxlogf(ERROR,
           "qmi-usb-transport: interface does not have the required 3 "
           "endpoints\n");
    usb_desc_iter_release(&iter);
    status = ZX_ERR_NOT_SUPPORTED;
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  uint8_t bulk_in_addr = 0;
  uint8_t bulk_out_addr = 0;
  uint8_t intr_addr = 0;
  uint16_t intr_max_packet = 0;
  uint16_t bulk_max_packet = 0;

  usb_descriptor_header_t* desc = usb_desc_iter_next(&iter);
  while (desc) {
    if (desc->bDescriptorType == USB_DT_ENDPOINT) {
      usb_endpoint_descriptor_t* endp = reinterpret_cast<usb_endpoint_descriptor_t*>(desc);
      if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
        if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
          bulk_out_addr = endp->bEndpointAddress;
          bulk_max_packet = usb_ep_max_packet(endp);
        }
      } else {
        if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
          bulk_in_addr = endp->bEndpointAddress;
        } else if (usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
          intr_addr = endp->bEndpointAddress;
          intr_max_packet = usb_ep_max_packet(endp);
        }
      }
    }
    desc = usb_desc_iter_next(&iter);
  }
  usb_desc_iter_release(&iter);

  if (bulk_in_addr == 0 || bulk_out_addr == 0 || intr_addr == 0) {
    zxlogf(ERROR, "qmi-usb-transport: failed to find one of the usb endpoints\n");
    zxlogf(ERROR, "qmi-usb-transport: bulkIn:%u, bulkOut:%u, intr:%u\n", bulk_in_addr,
           bulk_out_addr, intr_addr);
    status = ZX_ERR_INTERNAL;
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  if (intr_max_packet < 1 || bulk_max_packet < 1) {
    zxlogf(ERROR, "qmi-usb-transport: failed to find reasonable max packet sizes\n");
    zxlogf(ERROR, "qmi-usb-transport: intr_max_packet:%u, bulk_max_packet:%u\n", intr_max_packet,
           bulk_max_packet);
    status = ZX_ERR_INTERNAL;
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  rx_endpoint_delay_ = ETHERNET_INITIAL_RECV_DELAY;
  tx_endpoint_delay_ = ETHERNET_INITIAL_TRANSMIT_DELAY;

  // Reset by selecting default interface followed by data interface. We can't
  // start queueing transactions until this is complete.
  usb_set_interface(&usb, 8, 0);

  // set up interrupt
  status = usb_request_alloc(&int_buf, intr_max_packet, intr_addr, req_size);
  max_packet_size_ = bulk_max_packet;
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: failed to allocate for usb request: %s\n",
           zx_status_get_string(status));
    QmiBindFailedErr(status, int_buf);
    return status;
  }
  int_txn_buf_ = int_buf;

  // create port to watch for interrupts and channel messages
  status = zx_port_create(0, &qmi_channel_port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: failed to create a port: %s\n", zx_status_get_string(status));
    QmiBindFailedErr(status, int_buf);
    return status;
  }
  rx_endpoint_addr_ = bulk_in_addr;
  tx_endpoint_addr_ = bulk_out_addr;

  // Allocate tx transaction buffers
  // TODO uint16_t tx_buf_sz = qmi_ctx->mtu;
  uint16_t tx_buf_sz = 1024;
  size_t tx_buf_remain = kMaxTxBufSz;
  while (tx_buf_remain >= tx_buf_sz) {
    usb_request_t* tx_buf;
    zx_status_t alloc_result = usb_request_alloc(&tx_buf, tx_buf_sz, tx_endpoint_addr_, req_size);
    if (alloc_result != ZX_OK) {
      result = alloc_result;
      QmiBindFailedErr(status, int_buf);
      return status;
    }

    // As per the CDC-ECM spec, we need to send a zero-length packet to signify
    // the end of transmission when the endpoint max packet size is a factor of
    // the total transmission size
    tx_buf->header.send_zlp = true;

    zx_status_t status = usb_req_list_add_head(&tx_txn_bufs_, tx_buf, parent_req_size_);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    tx_buf_remain -= tx_buf_sz;
  }

  // Allocate rx transaction buffers
  // TODO(bwb) get correct buffer sizes from usb
  uint16_t rx_buf_sz = 1024;
  size_t rx_buf_remain = kMaxRxBufSz;
  while (rx_buf_remain >= rx_buf_sz) {
    usb_request_t* rx_buf;
    zx_status_t alloc_result = usb_request_alloc(&rx_buf, rx_buf_sz, rx_endpoint_addr_, req_size);
    if (alloc_result != ZX_OK) {
      result = alloc_result;
      QmiBindFailedErr(status, int_buf);
      return status;
    }

    usb_request_complete_t complete = {
        .callback = usb_read_complete,
        .ctx = this,
    };
    usb_request_queue(&usb_, rx_buf, &complete);
    rx_buf_remain -= rx_buf_sz;
  }

  // Set MAC addr
  mac_addr_[0] = 0x62;
  mac_addr_[1] = 0x77;
  mac_addr_[2] = 0x62;
  mac_addr_[3] = 0x62;
  mac_addr_[4] = 0x77;
  mac_addr_[5] = 0x62;

  // Kick off the handler thread
  int thread_result =
      thrd_create((thrd_t*)&int_thread_, (thrd_start_t)qmi_transport_thread, (void*)this);

  if (thread_result != thrd_success) {
    zxlogf(ERROR, "qmi-usb-transport: failed to create transport thread (%d)\n", thread_result);
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  status = DdkAdd("qmi-usb-transport", 0, nullptr, 0, ZX_PROTOCOL_QMI_TRANSPORT);
  if (status < 0) {
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  device_add_args_t eth_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "qmi-cdc-ethernet",
      .ctx = this,
      // sibling of qmi transport. Cleanup happens when qmi device unbinds
      .ops = &eth_qmi_ops,
      .proto_id = ZX_PROTOCOL_ETHERNET_IMPL,
      .proto_ops = &ethernet_impl_ops,
  };
  result = device_add(usb_device_, &eth_args, &eth_zxdev_);
  if (result < 0) {
    zxlogf(ERROR, "qmi-usb-transport: failed to add ethernet device: %d\n", (int)result);
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  return ZX_OK;
}
}  // namespace qmi_usb

static zx_status_t qmi_bind(void* ctx, zx_device_t* device) {
  zx_status_t status = ZX_OK;
  auto dev = std::make_unique<qmi_usb::Device>(device);
  status = dev->Bind();
  std::printf("%s\n", __func__);
  if (status != ZX_OK) {
    std::printf("qmi-usb-transport: could not bind: %d\n", status);
  } else {
    dev.release();
  }
  return status;
}

static zx_driver_ops_t qmi_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = qmi_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(qmi_usb, qmi_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, SIERRA_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, EM7565_PID),
ZIRCON_DRIVER_END(qmi_usb)
