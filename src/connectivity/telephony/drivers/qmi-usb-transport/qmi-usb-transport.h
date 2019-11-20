// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_TELEPHONY_DRIVERS_QMI_USB_TRANSPORT_QMI_USB_TRANSPORT_H_
#define SRC_CONNECTIVITY_TELEPHONY_DRIVERS_QMI_USB_TRANSPORT_QMI_USB_TRANSPORT_H_

#include <fuchsia/hardware/telephony/transport/llcpp/fidl.h>
#include <fuchsia/telephony/snoop/llcpp/fidl.h>
#include <lib/operation/ethernet.h>
#include <lib/sync/completion.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

// Binding info
#define SIERRA_VID 0x1199
#define EM7565_PID 0x9091
#define EM7565_PHY_ID 0x11
#define QMI_INTERFACE_NUM 8

// Port info
#define CHANNEL_MSG 1
#define INTERRUPT_MSG 2

namespace qmi_usb {

class Device : public ddk::Device<Device, ddk::UnbindableNew, ddk::Messageable>,
               llcpp::fuchsia::hardware::telephony::transport::Qmi::Interface {
 public:
  explicit Device(zx_device_t* parent);

  ~Device() = default;

  zx_status_t Bind();
  zx_status_t Unbind();
  void Release();

  zx_status_t SetChannelToDevice(zx_handle_t transport);
  zx_status_t SetNetworkStatusToDevice(bool connected);
  zx_status_t SetSnoopChannelToDevice(zx_handle_t channel);

  // TODO(jiamingw): Group similar declarations together. 
  zx_status_t CloseQmiChannel();
  zx_handle_t GetChannel();
  zx_status_t SetAsyncWait();
  zx_status_t QueueRequest(const uint8_t* data, size_t length, usb_request_t* req);
  zx_status_t SendLocked(ethernet_netbuf_t* netbuf);
  void QmiUpdateOnlineStatus(bool is_online);
  uint32_t GetMacAddr(uint8_t* buffer, uint32_t buffer_length);
  void QmiInterruptHandler(usb_request_t* request);
  zx_handle_t GetQmiChannelPort();
  void UsbRecv(usb_request_t* request);
  void UsbCdcIntHander(uint16_t packet_size);
  int EventLoop();
  void SnoopQmiMsgSend(uint8_t* msg_arr, uint32_t msg_arr_len,
                       ::llcpp::fuchsia::telephony::snoop::Direction direction);
  // Usb ops handler
  void UsbReadCompleteHandler(usb_request_t* request);
  void UsbWriteCompleteHandler(usb_request_t* request);
  // Ethernet ops table handler
  zx_status_t QmiEthernetStartHandler(const ethernet_ifc_protocol_t* ifc);
  void QmiEthernetStopHandler();
  void QmiEthernetQueueTxHandler(uint32_t options, ethernet_netbuf_t* netbuf,
                                 ethernet_impl_queue_tx_callback completion_cb, void* cookie);

  // DDK Mixin methods
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  // DDK ethernet_impl_protocol_ops methods
  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info);
  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc);
  void EthernetImplStop();
  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                   size_t data_size);

  void QmiBindFailedNoErr(usb_request_t* int_buf);
  void QmiBindFailedErr(zx_status_t status, usb_request_t* int_buf);

 private:
  // FIDL interface implementation
  void SetChannel(::zx::channel transport, SetChannelCompleter::Sync _completer) override;
  void SetNetwork(bool connected, SetNetworkCompleter::Sync _completer) override;
  void SetSnoopChannel(::zx::channel interface, SetSnoopChannelCompleter::Sync _completer) override;

  // Ethernet
  zx_device_t* eth_zxdev_;
  mtx_t ethernet_mutex_;
  ethernet_ifc_protocol_t ethernet_ifc_;

  // Device attributes
  uint8_t mac_addr_[ETH_MAC_SIZE];
  uint16_t ethernet_mtu_;  // TODO (jiamingw) confirm that this is used to replace magic number 1024

  // Connection attributes
  bool online_;          // TODO(jiamingw) change it to `is_online`
  bool device_unbound_;  // set to true when device is going away. Guarded by tx_mutex
  // TODO (jiamingw) - there is no code setting device_unbound_ to true
  //                   It will always be false in current code

  // Send context
  mtx_t tx_mutex_;
  uint8_t tx_endpoint_addr_;
  list_node_t tx_txn_bufs_;       // list of usb_request_t
  list_node_t tx_pending_infos_;  // list of txn_info_t, TODO(jiamingw): rename
  uint64_t tx_endpoint_delay_;    // wait time between 2 transmit requests

  // Receive context
  uint8_t rx_endpoint_addr_;
  uint64_t rx_endpoint_delay_;  // wait time between 2 recv requests
                                // TODO(jiamingw): rename it, this is delay budget

  // Interrupt handling
  usb_request_t* int_txn_buf_;
  thrd_t int_thread_;

  usb_protocol_t usb_;
  zx_device_t* usb_device_;
  size_t parent_req_size_;
  uint16_t max_packet_size_;

  // Port to watch for QMI messages on
  zx_handle_t qmi_channel_port_ = ZX_HANDLE_INVALID;
  zx_handle_t qmi_channel_ = ZX_HANDLE_INVALID;

  // Port for snoop QMI messages
  zx_handle_t snoop_channel_port_ = ZX_HANDLE_INVALID;
  zx_handle_t snoop_channel_ = ZX_HANDLE_INVALID;
};

}  // namespace qmi_usb

#endif  // SRC_CONNECTIVITY_TELEPHONY_DRIVERS_QMI_USB_TRANSPORT_QMI_USB_TRANSPORT_H_
