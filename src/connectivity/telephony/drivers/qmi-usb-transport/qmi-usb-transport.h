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

#include <array>

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

constexpr uint8_t kEthFrameHdrSize = 14;
constexpr uint8_t kEthertypeLen = 2;
constexpr std::array<uint8_t, kEthertypeLen> kEthertypeArpArr = {0x08, 0x06};
constexpr std::array<uint8_t, kEthertypeLen> kEthertypeIpv4Arr = {0x08, 0x0};
constexpr uint16_t kEthertypeArp = 0x0806;
constexpr uint16_t kEthertypeIpv4 = 0x0800;
constexpr uint16_t kArpSize = 28;
constexpr uint8_t kArpHdrSize = 8;
constexpr std::array<uint8_t, kArpHdrSize> kArpRespHdr = {0x00, 0x01, 0x08, 0x00,
                                                          0x06, 0x04, 0x00, 0x02};
constexpr std::array<uint8_t, kArpHdrSize> kArpReqHdr = {0x00, 0x01, 0x08, 0x00,
                                                         0x06, 0x04, 0x00, 0x01};
constexpr uint8_t kIpLenLoc = 2;
constexpr uint8_t kMacAddrLen = 6;
constexpr uint8_t kIpv4AddrLen = 4;
constexpr uint16_t kEthMtu = 1024;  // TODO (jiamingw): check if it can be 1500

constexpr uint8_t kByteShift1 = 8;

constexpr uint32_t kUsbCtrlEpMsgSizeMax = 2048;
constexpr uint32_t kUsbBulkInEpMsgSizeMax = 2048;
constexpr uint32_t kUsbBulkOutEpMsgSizeMax = 2048;

struct EthTxStats {
  uint32_t arp_req_cnt;
  uint32_t arp_resp_cnt;
  uint32_t arp_dropped_cnt;
  uint32_t eth_type_unsupported_cnt;
  uint32_t eth_dropped_cnt;
  uint32_t ipv4_tx_succeed_cnt;
  uint32_t ipv4_tx_dropped_cnt;
};

struct ArpFrameHdr {
  uint16_t hw_type;
  uint16_t proto_type;
  uint8_t hw_addr_len;
  uint8_t proto_addr_len;
  uint16_t opcode;
} __PACKED;

struct ArpFrame {
  ArpFrameHdr arp_hdr;
  uint8_t src_mac_addr[kMacAddrLen];
  uint8_t src_ip_addr[kIpv4AddrLen];
  uint8_t dst_mac_addr[kMacAddrLen];
  uint8_t dst_ip_addr[kIpv4AddrLen];
} __PACKED;

struct EthFrameHdr {
  uint8_t dst_mac_addr[kMacAddrLen];
  uint8_t src_mac_addr[kMacAddrLen];
  uint16_t ethertype;
} __PACKED;

struct EthArpFrame {
  EthFrameHdr eth_hdr;
  ArpFrame arp;
} __PACKED;

struct EthFrame {
  EthFrameHdr eth_hdr;
  uint8_t eth_payload[];
} __PACKED;

struct IpPktHdr {
  uint32_t version : 4;
  uint32_t ihl : 4;
  uint32_t dscp : 6;
  uint32_t ecn : 2;
  uint32_t total_length : 16;
} __PACKED;

// Fake mac address of the ethernet device that meets the mac address
// format requirement: unicast and locally administered
constexpr std::array<uint8_t, kMacAddrLen> kFakeMacAddr = {0x02, 0x47, 0x4f, 0x4f, 0x47, 0x4c};

class Device : public ddk::Device<Device, ddk::Unbindable, ddk::Messageable>,
               llcpp::fuchsia::hardware::telephony::transport::Qmi::Interface {
 public:
  explicit Device(zx_device_t* parent);

  ~Device() = default;

  zx_status_t Bind();
  void Release();

  zx_status_t SetChannelToDevice(zx_handle_t transport);
  zx_status_t SetNetworkStatusToDevice(bool connected);
  zx_status_t SetSnoopChannelToDevice(zx_handle_t channel);

  // TODO(jiamingw): Group similar declarations together.
  zx_status_t CloseQmiChannel();
  zx_handle_t GetChannel();
  zx_status_t SetAsyncWait();
  zx_status_t HandleArpReq(const ArpFrame& arp_frame);
  void GenEthArpResp(const ArpFrame& req, EthArpFrame* resp);
  void SetEthDestMacAddr(const uint8_t* mac_addr_ptr);
  void GenInboundEthFrameHdr(EthFrameHdr* eth_frame_hdr);
  zx_status_t QueueUsbRequestHandler(const uint8_t* ip, size_t length, usb_request_t* req);
  zx_status_t HandleQueueUsbReq(const uint8_t* data, size_t length, usb_request_t* req);
  zx_status_t SendLocked(const uint8_t* byte_data, size_t length);
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

  void EthClientInit(const ethernet_ifc_protocol_t* ifc);
  void EthTxListNodeInit();

  // DDK Mixin methods
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbind(ddk::UnbindTxn txn);
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
  void SetChannel(::zx::channel transport, SetChannelCompleter::Sync& _completer) override;
  void SetNetwork(bool connected, SetNetworkCompleter::Sync& _completer) override;
  void SetSnoopChannel(::zx::channel interface,
                       SetSnoopChannelCompleter::Sync& _completer) override;

  // Ethernet
  zx_device_t* eth_zxdev_;
  fbl::Mutex eth_mutex_;
  std::unique_ptr<ddk::EthernetIfcProtocolClient> eth_ifc_ptr_;
  std::unique_ptr<std::array<uint8_t, kMacAddrLen>> eth_dst_mac_addr_;
  EthTxStats eth_tx_stats_;

  // Device attributes
  uint16_t ethernet_mtu_;  // TODO (jiamingw) confirm that this is used to replace magic number 1024

  // Connection attributes
  bool online_;          // TODO(jiamingw) change it to `is_online`
  bool device_unbound_;  // set to true when device is going away. Guarded by tx_mutex
  // TODO (jiamingw) - there is no code setting device_unbound_ to true
  //                   It will always be false in current code

  // Send context
  fbl::Mutex tx_mutex_;
  uint8_t tx_endpoint_addr_;
  list_node_t tx_txn_bufs_ __TA_GUARDED(tx_mutex_);  // list of usb_request_t
  list_node_t tx_pending_infos_
      __TA_GUARDED(tx_mutex_);  // list of txn_info_t, TODO(jiamingw): rename
  uint64_t tx_endpoint_delay_;  // wait time between 2 transmit requests

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
