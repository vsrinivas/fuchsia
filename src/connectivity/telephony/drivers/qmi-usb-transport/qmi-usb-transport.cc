// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qmi-usb-transport.h"

#include <endian.h>
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
  zxlogf(INFO, "qmi-usb-transport: getting channel from transport");
  zx_status_t result = ZX_OK;

  if (qmi_channel_ != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "qmi-usb-transport: already bound, failing");
    result = ZX_ERR_ALREADY_BOUND;
  } else if (channel == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "qmi-usb-transport: invalid channel handle");
    result = ZX_ERR_BAD_HANDLE;
  } else {
    qmi_channel_ = channel;
  }
  return result;
}

void Device::SetEthDestMacAddr(const uint8_t* mac_addr_ptr) {
  if (eth_dst_mac_addr_ == nullptr) {
    eth_dst_mac_addr_ = std::make_unique<std::array<uint8_t, kMacAddrLen>>();
  } else {
    zxlogf(INFO, "qmi-usb-transport: overwriting eth dest mac addr");
  }

  std::copy(mac_addr_ptr, &mac_addr_ptr[kMacAddrLen], eth_dst_mac_addr_.get()->begin());
}

zx_status_t Device::HandleArpReq(const ArpFrame& arp_frame) {
  if (memcmp(kArpReqHdr.data(), &arp_frame.arp_hdr, sizeof(kArpReqHdr)) != 0) {
    zxlogf(ERROR, "qmi-usb-transport: invalid arp request");
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  SetEthDestMacAddr(arp_frame.src_mac_addr);

  EthArpFrame eth_arp_resp;
  GenEthArpResp(arp_frame, &eth_arp_resp);

  // TODO (jiamingw): understand the reason to sleep here.
  zx_nanosleep(zx_deadline_after(ZX_USEC(tx_endpoint_delay_)));
  fbl::AutoLock lock(&eth_mutex_);
  if (eth_ifc_ptr_ && eth_ifc_ptr_->is_valid()) {
    // TODO (jiamingw): Log arp response.
    zxlogf(INFO, "qmi-usb-transport: Replying Arp Msg");
    eth_ifc_ptr_->Recv(reinterpret_cast<const uint8_t*>(&eth_arp_resp), kArpSize + kEthFrameHdrSize,
                       0);
  }

  return ZX_OK;
}

void Device::GenEthArpResp(const ArpFrame& req, EthArpFrame* resp) {
  // TODO (fxb/40051): Generate fake mac address to support multiple
  // cellular devices.
  // Eth header
  // eth_dst_mac_addr_ is ensured to be not null when calling this method.
  std::copy(eth_dst_mac_addr_->begin(), eth_dst_mac_addr_->end(), resp->eth_hdr.dst_mac_addr);
  std::copy(kFakeMacAddr.begin(), kFakeMacAddr.end(), resp->eth_hdr.src_mac_addr);
  resp->eth_hdr.ethertype = betoh16(kEthertypeArp);
  // Eth payload.
  std::copy(kArpRespHdr.begin(), kArpRespHdr.end(), reinterpret_cast<uint8_t*>(&resp->arp.arp_hdr));
  std::copy(kFakeMacAddr.begin(), kFakeMacAddr.end(), resp->arp.src_mac_addr);
  std::copy(req.dst_ip_addr, &req.dst_ip_addr[kIpv4AddrLen], resp->arp.src_ip_addr);
  std::copy(eth_dst_mac_addr_->begin(), eth_dst_mac_addr_->end(), resp->arp.dst_mac_addr);
  std::copy(req.src_ip_addr, &req.src_ip_addr[kIpv4AddrLen], resp->arp.dst_ip_addr);
}

zx_status_t Device::QueueUsbRequestHandler(const uint8_t* ip, size_t length, usb_request_t* req) {
  auto ip_hdr = reinterpret_cast<const IpPktHdr*>(ip);
  size_t ip_length = betoh16(ip_hdr->total_length);
  zxlogf(INFO, "qmi-usb-transport: ip: x%x x%x", ip[0], ip[1]);

  if (ip_length != length) {
    zxlogf(ERROR,
           "qmi-usb-transport: length of IP packet does not match length in eth "
           "frame! %zd/%zd \n",
           ip_length, length);
    return ZX_ERR_IO;
  }

  ssize_t bytes_copied = usb_request_copy_to(req, ip, ip_length, 0);
  if (bytes_copied < 0) {
    zxlogf(ERROR, "qmi-usb-transport: failed to copy data into send txn (error %zd)", bytes_copied);
    return ZX_ERR_IO;
  }

  if (static_cast<size_t>(bytes_copied) != ip_length) {
    zxlogf(ERROR, "qmi-usb-transport: expect to copy %zu bytes but only copied %zd", ip_length,
           bytes_copied);
    return ZX_ERR_IO;
  }

  usb_request_complete_t complete = {
      .callback = usb_write_complete,
      .ctx = this,
  };
  req->header.length = bytes_copied;
  // TODO (jiamingw): logging IP packet ((uintptr_t)req->virt) + req->offset) with length ip_length.
  zxlogf(INFO, "qmi-usb-transport: tx IP pkt");
  usb_request_queue(&usb_, req, &complete);
  return ZX_OK;
}

zx_status_t Device::HandleQueueUsbReq(const uint8_t* data, size_t length, usb_request_t* req) {
  zxlogf(INFO, "qmi-usb-transport: sending data to modem");
  zx_status_t res = QueueUsbRequestHandler(data, length, req);
  if (res != ZX_OK) {
    eth_tx_stats_.ipv4_tx_dropped_cnt += 1;
    return res;
  }
  eth_tx_stats_.ipv4_tx_succeed_cnt += 1;
  return ZX_OK;
}

zx_status_t inline Device::SendLocked(const uint8_t* byte_data, size_t length) {
  // Make sure that we can get all of the tx buffers we need to use
  usb_request_t* tx_req = usb_req_list_remove_head(&tx_txn_bufs_, parent_req_size_);
  if (tx_req == nullptr) {
    return ZX_ERR_SHOULD_WAIT;
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(tx_endpoint_delay_)));
  zx_status_t status;
  if ((status = HandleQueueUsbReq(byte_data, length, tx_req)) != ZX_OK) {
    // Add packet back to original place if it is not being sent.
    zx_status_t add_status = usb_req_list_add_head(&tx_txn_bufs_, tx_req, parent_req_size_);
    ZX_DEBUG_ASSERT(add_status == ZX_OK);
    return status;
  }

  return ZX_OK;
}

void Device::QmiUpdateOnlineStatus(bool is_online) {
  fbl::AutoLock lock(&eth_mutex_);
  if ((is_online && online_) || (!is_online && !online_)) {
    return;
  }

  if (is_online) {
    zxlogf(INFO, "qmi-usb-transport: connected to network");
    online_ = true;
    if (eth_ifc_ptr_ && eth_ifc_ptr_->is_valid()) {
      eth_ifc_ptr_->Status(online_ ? ETHERNET_STATUS_ONLINE : 0);
    } else {
      zxlogf(ERROR, "qmi-usb-transport: not connected to ethermac interface");
    }
  } else {
    zxlogf(INFO, "qmi-usb-transport: no connection to network");
    online_ = false;
    if (eth_ifc_ptr_ && eth_ifc_ptr_->is_valid()) {
      eth_ifc_ptr_->Status(0);
    }
  }
}

zx_status_t Device::SetAsyncWait() {
  return zx_object_wait_async(qmi_channel_, qmi_channel_port_, CHANNEL_MSG,
                              ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0);
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
      zxlogf(ERROR, "qmi-usb-transport: timed out: %s", zx_status_get_string(status));
    } else if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
      zxlogf(INFO, "qmi-usb-transport: snoop channel peer closed");
      snoop_channel_ = ZX_HANDLE_INVALID;
    }
  }

  status = ZX_OK;
  if (snoop_channel_ != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "snoop channel already connected");
    status = ZX_ERR_ALREADY_BOUND;
  } else if (channel == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "get invalid snoop channel handle");
    status = ZX_ERR_BAD_HANDLE;
  } else {
    snoop_channel_ = channel;
    zx_object_wait_async(snoop_channel_, snoop_channel_port_, 0, ZX_CHANNEL_PEER_CLOSED, 0);
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
    completer.ReplySuccess();
    zx_status_t status = SetAsyncWait();
    if (status != ZX_OK) {
      CloseQmiChannel();
    }
  } else {
    completer.ReplyError(set_channel_res);
  }
}

void Device::SetNetwork(bool connected, SetNetworkCompleter::Sync completer) {
  QmiUpdateOnlineStatus(connected);
  completer.Reply();
}

void Device::SetSnoopChannel(::zx::channel interface, SetSnoopChannelCompleter::Sync completer) {
  zx_status_t set_snoop_res = SetSnoopChannelToDevice(interface.release());
  if (set_snoop_res == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(static_cast<uint32_t>(set_snoop_res));
  }
}

zx_status_t Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  telephony_transport::Qmi::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void Device::Release() { delete this; }

void Device::DdkRelease() {
  zxlogf(INFO, "qmi-usb-transport: releasing device");
  Release();
}

void Device::DdkUnbind(ddk::UnbindTxn txn) {
  txn.Reply();
}

uint32_t Device::GetMacAddr(uint8_t* buffer, uint32_t buffer_length) {
  if (buffer == nullptr) {
    return 0;
  }
  uint32_t copy_length = std::min((uint32_t)sizeof(kFakeMacAddr), buffer_length);
  std::copy(kFakeMacAddr.begin(), kFakeMacAddr.end(), buffer);
  return copy_length;
}

zx_status_t Device::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  zxlogf(INFO, "qmi-usb-transport: %s called", __FUNCTION__);
  // No options are supported
  if (options) {
    zxlogf(ERROR, "qmi-usb-transport: unexpected options to ethernet_query");
    return ZX_ERR_INVALID_ARGS;
  }
  memset(info, 0, sizeof(*info));
  info->mtu = kEthMtu;
  GetMacAddr(info->mac, sizeof(info->mac));
  info->netbuf_size = sizeof(txn_info_t);
  return ZX_OK;
}

zx_status_t Device::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  zxlogf(INFO, "qmi-usb-transport: EthernetImplStart called");
  fbl::AutoLock lock(&eth_mutex_);
  if (eth_ifc_ptr_ && eth_ifc_ptr_->is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  eth_ifc_ptr_ = std::make_unique<ddk::EthernetIfcProtocolClient>(ifc);
  return ZX_OK;
}

zx_status_t Device::EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                         size_t data_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

void Device::EthernetImplStop() {
  zxlogf(INFO, "qmi-usb-transport: %s called", __FUNCTION__);
  fbl::AutoLock lock(&eth_mutex_);
  eth_ifc_ptr_.reset();
}

void Device::EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                 ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  // TODO (jiamingw): Log netbuf->data_buffer with length netbuf->data_size.
  zxlogf(INFO, "qmi-usb-transport: transmitting outbound data plane msg:");

  size_t length = netbuf->data_size;

  if (length < kEthFrameHdrSize) {
    zxlogf(ERROR, "qmi-usb-transport: tx eth frame too short (length: %zx) ", length);
    eth_tx_stats_.eth_dropped_cnt += 1;
  }
  size_t eth_payload_len = length - kEthFrameHdrSize;

  // Check data type. Only Arp or IPv4 packet will be handled.
  const EthFrameHdr* eth_hdr = static_cast<const EthFrameHdr*>(netbuf->data_buffer);
  switch (betoh16(eth_hdr->ethertype)) {
    // Arp request. Send back Arp response.
    case kEthertypeArp: {
      eth_tx_stats_.arp_req_cnt += 1;
      if (eth_payload_len < kArpSize) {
        zxlogf(ERROR, "qmi-usb-transport: arp req too short");
        return;
      }

      auto eth_arp = static_cast<const EthArpFrame*>(netbuf->data_buffer);
      zx_status_t res = HandleArpReq(eth_arp->arp);
      if (res != ZX_OK) {
        eth_tx_stats_.arp_dropped_cnt += 1;
      }
      break;
    }
    // IPv4 packet. Send to modem.
    case kEthertypeIpv4: {
      zx_status_t status = ZX_OK;
      txn_info_t* txn = containerof(netbuf, txn_info_t, netbuf);
      txn->completion_cb = completion_cb;
      txn->cookie = cookie;

      if (length > kEthMtu || length == 0) {
        complete_txn(txn, ZX_ERR_INVALID_ARGS);
        return;
      }

      {
        fbl::AutoLock lock(&tx_mutex_);

        if (device_unbound_) {
          status = ZX_ERR_IO_NOT_PRESENT;
        } else {
          auto eth_ip = static_cast<const EthFrame*>(netbuf->data_buffer);
          status = SendLocked(eth_ip->eth_payload, eth_payload_len);
          if (status == ZX_ERR_SHOULD_WAIT) {
            // No buffers available, queue it up
            list_add_tail(&tx_pending_infos_, &txn->node);
          }
        }
      }

      if (status != ZX_ERR_SHOULD_WAIT) {
        complete_txn(txn, status);
      }
      break;
    }
    default:
      zxlogf(ERROR, "qmi-usb-transport: ethertype 0x%x not supported", eth_hdr->ethertype);
      eth_tx_stats_.eth_dropped_cnt += 1;
      break;
  }
  return;
}

#define DEV(c) static_cast<Device*>(c)
static ethernet_impl_protocol_ops_t ethernet_impl_ops = {
    .query = [](void* ctx, uint32_t options, ethernet_info_t* info) -> zx_status_t {
      return DEV(ctx)->EthernetImplQuery(options, info);
    },
    .stop = [](void* ctx) { DEV(ctx)->EthernetImplStop(); },
    .start = [](void* ctx, const ethernet_ifc_protocol_t* ifc) -> zx_status_t {
      return DEV(ctx)->EthernetImplStart(ifc);
    },
    .queue_tx =
        [](void* ctx, uint32_t options, ethernet_netbuf_t* netbuf,
           ethernet_impl_queue_tx_callback completion_cb,
           void* cookie) { DEV(ctx)->EthernetImplQueueTx(options, netbuf, completion_cb, cookie); },
    .set_param = [](void* ctx, uint32_t param, int32_t value, const void* data, size_t data_size)
        -> zx_status_t { return DEV(ctx)->EthernetImplSetParam(param, value, data, data_size); },
};
#undef DEV

static zx_protocol_device_t eth_qmi_ops = {
    .version = DEVICE_OPS_VERSION,
};

void Device::QmiInterruptHandler(usb_request_t* request) {
  zxlogf(INFO, "request->response.actual: %lu", request->response.actual);
  if (request->response.actual < sizeof(usb_cdc_notification_t)) {
    zxlogf(ERROR, "qmi-usb-transport: ignored interrupt (size = %ld)",
           (long)request->response.actual);
    return;
  }

  usb_cdc_notification_t usb_req;
  usb_request_copy_from(request, &usb_req, sizeof(usb_cdc_notification_t), 0);

  // TODO (jiamingw): confirm this check is unnecessary
  uint16_t packet_size = max_packet_size_;
  if (packet_size > kUsbCtrlEpMsgSizeMax) {
    zxlogf(ERROR, "qmi-usb-transport: packet too big: %d", packet_size);
    return;
  }

  switch (usb_req.bNotification) {
    case USB_CDC_NC_RESPONSE_AVAILABLE:
      UsbCdcIntHander(packet_size);
      break;
    default:
      zxlogf(ERROR, "qmi-usb-transport: Unknown Notification Type for QMI: %d",
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
    zxlogf(WARNING, "qmi-usb-transport: recieving USB CDC frames without a channel");
    return;
  }
  status = zx_channel_write(qmi_channel_, 0, buffer, sizeof(buffer), nullptr, 0);
  if (status < 0) {
    zxlogf(ERROR, "qmi-usb-transport: failed to write message to channel: %s",
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
  telephony_snoop::Message snoop_msg =
      telephony_snoop::Message::WithQmiMessage(fidl::unowned_ptr(&qmi_msg));
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
  zxlogf(INFO, "successfully queued int req");
  if (max_packet_size_ > kUsbCtrlEpMsgSizeMax) {
    zxlogf(ERROR, "qmi-usb-transport: packet too big: %d", max_packet_size_);
    return ZX_ERR_IO_REFUSED;
  }
  uint8_t buffer[max_packet_size_];
  uint32_t length = sizeof(buffer);
  zx_port_packet_t packet;
  while (true) {
    zx_status_t status = zx_port_wait(qmi_channel_port_, ZX_TIME_INFINITE, &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "qmi-usb-transport: timed out: %s", zx_status_get_string(status));
      continue;
    }
    if (packet.key == CHANNEL_MSG) {
      if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
        zxlogf(INFO, "qmi-usb-transport: channel closed");
        status = CloseQmiChannel();
        if (status != ZX_OK) {
          zxlogf(ERROR, "qmi-usb-transport: failed to close QMI channel: %s",
                 zx_status_get_string(status));
        }
        continue;
      }
      status =
          zx_channel_read(qmi_channel_, 0, buffer, nullptr, sizeof(buffer), 0, &length, nullptr);
      if (status != ZX_OK) {
        zxlogf(ERROR, "qmi-usb-transport: failed to read channel: %s",
               zx_status_get_string(status));
        return status;
      }
      status = usb_control_out(&usb_, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                               USB_CDC_SEND_ENCAPSULATED_COMMAND, 0, 8, ZX_TIME_INFINITE, buffer,
                               length);
      if (status != ZX_OK) {
        zxlogf(ERROR, "qmi-usb-transport: got an bad status from usb_control_out: %s",
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
        zxlogf(INFO, "qmi-usb-transport: terminating interrupt handling thread");
        return txn->response.status;
      }
    } else {
      zxlogf(ERROR, "qmi-usb-transport: invalid pkt key");
    }
  }  // while(true)
}

static int qmi_transport_thread(void* ctx) {
  Device* device_ptr = static_cast<Device*>(ctx);
  return device_ptr->EventLoop();
}

void Device::GenInboundEthFrameHdr(EthFrameHdr* eth_frame_hdr) {
  // Dest mac addr.
  std::copy(eth_dst_mac_addr_->begin(), eth_dst_mac_addr_->end(), eth_frame_hdr->dst_mac_addr);
  // Src mac addr.
  std::copy(&kFakeMacAddr[0], &kFakeMacAddr[kMacAddrLen], eth_frame_hdr->src_mac_addr);
  // Ethertype.
  eth_frame_hdr->ethertype = betoh16(kEthertypeIpv4);
  return;
}

// Note: the assumption made here is that no rx transmissions will be processed
// in parallel, so we do not maintain an rx mutex.
void Device::UsbRecv(usb_request_t* request) {
  size_t eth_frame_payload_len = request->response.actual;

  uint8_t* eth_frame_payload;
  zx_status_t status = usb_request_mmap(request, reinterpret_cast<void**>(&eth_frame_payload));
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: usb_request_mmap failed with status %d", status);
    return;
  }

  if (eth_frame_payload_len > kUsbBulkInEpMsgSizeMax) {
    zxlogf(ERROR, "qmi-usb-transport: recieved usb packet is too large: %zd",
           eth_frame_payload_len);
    return;
  }

  // TODO (jiamingw): Log message eth_frame_payload.
  zxlogf(INFO, "qmi-usb-transport: getting inbound data plane msg");

  if (eth_dst_mac_addr_ == nullptr) {
    zxlogf(ERROR, "qmi-usb-transport: no eth mac addr, cannot send eth frame");
    return;
  }

  uint8_t eth_frame_arr[eth_frame_payload_len + kEthFrameHdrSize];
  auto eth_ip = reinterpret_cast<EthFrame*>(eth_frame_arr);
  GenInboundEthFrameHdr(&eth_ip->eth_hdr);

  // Copy ethernet frame payload.
  std::copy(eth_frame_payload, &eth_frame_payload[eth_frame_payload_len], eth_ip->eth_payload);
  fbl::AutoLock lock(&eth_mutex_);
  if (eth_ifc_ptr_ && eth_ifc_ptr_->is_valid()) {
    eth_ifc_ptr_->Recv(eth_frame_arr, eth_frame_payload_len + kEthFrameHdrSize, 0);
  }
}

static void usb_read_complete(void* ctx, usb_request_t* request) {
  Device* device_ptr = static_cast<Device*>(ctx);
  device_ptr->UsbReadCompleteHandler(request);
}

void Device::UsbReadCompleteHandler(usb_request_t* request) {
  if (request->response.status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: usb_read_complete called with status %d",
           (int)request->response.status);
  }

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(ERROR, "qmi-usb-transport: resetting receive endpoint");
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
  fbl::AutoLock tx_lock(&tx_mutex_);

  zx_status_t status = usb_req_list_add_tail(&tx_txn_bufs_, request, parent_req_size_);
  ZX_DEBUG_ASSERT(status == ZX_OK);

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(ERROR, "qmi-usb-transport: resetting transmit endpoint");
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
    if ((send_status =
             SendLocked(&static_cast<const uint8_t*>(txn->netbuf.data_buffer)[kEthFrameHdrSize],
                        (txn->netbuf.data_size - kEthFrameHdrSize)) != ZX_ERR_SHOULD_WAIT)) {
      list_remove_head(&tx_pending_infos_);
      additional_tx_queued = true;
    }
  }

  tx_lock.release();

  fbl::AutoLock eth_lock(&eth_mutex_);
  if (additional_tx_queued) {
    complete_txn(txn, send_status);
  }
  // When the interface is offline, the transaction will complete with status
  // set to ZX_ERR_IO_NOT_PRESENT. There's not much we can do except ignore it.
}

static void usb_write_complete(void* ctx, usb_request_t* request) {
  Device* device_ptr = static_cast<Device*>(ctx);
  device_ptr->UsbWriteCompleteHandler(request);
}

Device::Device(zx_device_t* parent)
    : ddk::Device<Device, ddk::Unbindable, ddk::Messageable>(parent) {
  usb_device_ = parent;
}

void Device::QmiBindFailedNoErr(usb_request_t* int_buf) {
  if (int_buf) {
    usb_request_release(int_buf);
  }
}

void Device::QmiBindFailedErr(zx_status_t status, usb_request_t* int_buf) {
  zxlogf(ERROR, "qmi-usb-transport: bind failed: %s", zx_status_get_string(status));
  QmiBindFailedNoErr(int_buf);
}

void Device::EthClientInit(const ethernet_ifc_protocol_t* ifc) {
  eth_ifc_ptr_.reset(new ddk::EthernetIfcProtocolClient(ifc));
}

void Device::EthTxListNodeInit() {
  list_initialize(&tx_txn_bufs_);
  list_initialize(&tx_pending_infos_);
}

zx_status_t Device::Bind() __TA_NO_THREAD_SAFETY_ANALYSIS {
  zx_status_t status;
  usb_request_t* int_buf = nullptr;

  // Set up USB stuff
  usb_protocol_t usb;
  status = device_get_protocol(usb_device_, ZX_PROTOCOL_USB, &usb);
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: get protocol failed: %s", zx_status_get_string(status));
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  // Initialize context
  memcpy(&usb_, &usb, sizeof(usb_));
  EthTxListNodeInit();

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

  usb_descriptor_header_t* desc;
  while ((desc = usb_desc_iter_peek(&iter)) != NULL) {
    if (desc->bDescriptorType == USB_DT_ENDPOINT) {
      usb_endpoint_descriptor_t* endp = reinterpret_cast<usb_endpoint_descriptor_t*>(
          usb_desc_iter_get_structure(&iter, sizeof(usb_endpoint_descriptor_t)));
      if (endp == NULL) {
        break;
      }
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
    usb_desc_iter_advance(&iter);
  }
  usb_desc_iter_release(&iter);

  if (bulk_in_addr == 0 || bulk_out_addr == 0 || intr_addr == 0) {
    zxlogf(ERROR, "qmi-usb-transport: failed to find one of the usb endpoints");
    zxlogf(ERROR, "qmi-usb-transport: bulkIn:%u, bulkOut:%u, intr:%u", bulk_in_addr, bulk_out_addr,
           intr_addr);
    status = ZX_ERR_INTERNAL;
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  if (intr_max_packet < 1 || bulk_max_packet < 1) {
    zxlogf(ERROR, "qmi-usb-transport: failed to find reasonable max packet sizes");
    zxlogf(ERROR, "qmi-usb-transport: intr_max_packet:%u, bulk_max_packet:%u", intr_max_packet,
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
    zxlogf(ERROR, "qmi-usb-transport: failed to allocate for usb request: %s",
           zx_status_get_string(status));
    QmiBindFailedErr(status, int_buf);
    return status;
  }
  int_txn_buf_ = int_buf;

  // create port to watch for interrupts and channel messages
  status = zx_port_create(0, &qmi_channel_port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: failed to create a port: %s", zx_status_get_string(status));
    QmiBindFailedErr(status, int_buf);
    return status;
  }
  rx_endpoint_addr_ = bulk_in_addr;
  tx_endpoint_addr_ = bulk_out_addr;

  // Allocate tx transaction buffers
  // TODO uint16_t tx_buf_sz = qmi_ctx->mtu;
  uint16_t tx_buf_sz = kEthMtu;
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
  uint16_t rx_buf_sz = kEthMtu;
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

  // Kick off the handler thread
  int thread_result =
      thrd_create((thrd_t*)&int_thread_, (thrd_start_t)qmi_transport_thread, (void*)this);

  if (thread_result != thrd_success) {
    zxlogf(ERROR, "qmi-usb-transport: failed to create transport thread (%d)", thread_result);
    QmiBindFailedErr(status, int_buf);
    return status;
  }

  status = DdkAdd(ddk::DeviceAddArgs("qmi-usb-transport").set_proto_id(ZX_PROTOCOL_QMI_TRANSPORT));
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
    zxlogf(ERROR, "qmi-usb-transport: failed to add ethernet device: %d", (int)result);
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
