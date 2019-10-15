// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_TELEPHONY_DRIVERS_QMI_USB_TRANSPORT_QMI_USB_TRANSPORT_H_
#define SRC_CONNECTIVITY_TELEPHONY_DRIVERS_QMI_USB_TRANSPORT_QMI_USB_TRANSPORT_H_

#include <fuchsia/hardware/telephony/transport/llcpp/fidl.h>
#include <fuchsia/telephony/snoop/llcpp/fidl.h>
#include <lib/sync/completion.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb/cdc.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

// clang-format off

// binding info
#define SIERRA_VID        0x1199
#define EM7565_PID        0x9091
#define EM7565_PHY_ID     0x11
#define QMI_INTERFACE_NUM 8

// port info
#define CHANNEL_MSG 1
#define INTERRUPT_MSG 2

// qmi usb transport device
typedef struct qmi_ctx {
  // Interrupt handling
  usb_request_t* int_txn_buf;
  sync_completion_t completion;
  thrd_t int_thread;

  uint16_t max_packet_size;

  // Port to watch for QMI messages on
  zx_handle_t channel_port;
  zx_handle_t channel;

  // Port for snoop QMI messages
  zx_handle_t snoop_channel_port;
  zx_handle_t snoop_channel;

  usb_protocol_t usb;
  zx_device_t* usb_device;
  zx_device_t* zxdev;
  size_t parent_req_size;

  // Ethernet
  zx_device_t* eth_zxdev;

  mtx_t ethernet_mutex;
  ethernet_ifc_protocol_t ethernet_ifc;

  // Device attributes
  uint8_t mac_addr[ETH_MAC_SIZE];
  uint16_t mtu;

  // Connection attributes
  bool online;  // TODO(jiamingw) change it to `is_online`

  // Send context
  mtx_t tx_mutex;
  uint8_t tx_endpoint_addr;
  uint16_t endpoint_size;
  list_node_t tx_txn_bufs;       // list of usb_request_t
  list_node_t tx_pending_infos;  // list of txn_info_t, TODO(jiamingw): rename
  uint64_t tx_endpoint_delay;  // wait time between 2 transmit requests

  bool unbound;  // set to true when device is going away. Guarded by tx_mutex

  // Receive context
  uint8_t rx_endpoint_addr;
  uint64_t rx_endpoint_delay;  // wait time between 2 recv requests
                               // TODO(jiamingw): rename it, this is delay budget
} qmi_ctx_t;

class Device : public ::llcpp::fuchsia::hardware::telephony::transport::Qmi::Interface {
 public:
  Device(qmi_ctx_t* ctx) : qmi_ctx(ctx) {}
  void SetChannel(::zx::channel transport, SetChannelCompleter::Sync completer) override;
  void SetNetwork(bool connected, SetNetworkCompleter::Sync completer) override;
  void SetSnoopChannel(::zx::channel interface, SetSnoopChannelCompleter::Sync completer) override;

 private:
  qmi_ctx_t* qmi_ctx;
};

#endif  // SRC_CONNECTIVITY_TELEPHONY_DRIVERS_QMI_USB_TRANSPORT_QMI_USB_TRANSPORT_H_
