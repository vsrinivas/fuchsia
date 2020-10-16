// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_LIB_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_LIB_H_

#include <lib/sync/completion.h>
#include <threads.h>
#include <zircon/hw/usb/cdc.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/ethernet.h>
#include <usb/usb.h>

#include "src/lib/listnode/listnode.h"

__BEGIN_CDECLS

#define CDC_SUPPORTED_VERSION 0x0110 /* 1.10 */

extern const char* module_name;

typedef struct {
  uint8_t addr;
  uint16_t max_packet_size;
} ecm_endpoint_t;

typedef struct {
  zx_device_t* zxdev;
  zx_device_t* usb_device;
  usb_protocol_t usb;
  // Ethernet lock -- must be acquired after tx_mutex
  // when both locks are held.
  mtx_t ethernet_mutex;
  ethernet_ifc_protocol_t ethernet_ifc;

  // Device attributes
  uint8_t mac_addr[ETH_MAC_SIZE];
  uint16_t mtu;

  // Connection attributes
  bool online;
  uint32_t ds_bps;
  uint32_t us_bps;

  // Interrupt handling
  ecm_endpoint_t int_endpoint;
  usb_request_t* int_txn_buf;
  sync_completion_t completion;
  thrd_t int_thread;

  // Send context
  // TX lock -- Must be acquired before ethernet_mutex
  // when both locks are held.
  mtx_t tx_mutex;
  ecm_endpoint_t tx_endpoint;
  list_node_t tx_txn_bufs;       // list of usb_request_t
  list_node_t tx_pending_infos;  // list of txn_info_t
  bool unbound;                  // set to true when device is going away. Guarded by tx_mutex
  uint64_t tx_endpoint_delay;    // wait time between 2 transmit requests

  size_t parent_req_size;

  // Receive context
  ecm_endpoint_t rx_endpoint;
  uint64_t rx_endpoint_delay;  // wait time between 2 recv requests
  uint16_t rx_packet_filter;
} ecm_ctx_t;

typedef struct txn_info {
  ethernet_netbuf_t netbuf;
  ethernet_impl_queue_tx_callback completion_cb;
  void* cookie;
  list_node_t node;
} txn_info_t;

zx_status_t parse_usb_descriptor(usb_desc_iter_t* usb, usb_endpoint_descriptor_t** int_ep,
                                 usb_endpoint_descriptor_t** tx_ep,
                                 usb_endpoint_descriptor_t** rx_ep,
                                 usb_interface_descriptor_t** default_ifc,
                                 usb_interface_descriptor_t** data_ifc, ecm_ctx_t* ecm_ctx);

__END_CDECLS

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_LIB_H_
