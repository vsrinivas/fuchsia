// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_LIB_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_LIB_H_

#include <fuchsia/hardware/ethernet/c/banjo.h>
#include <fuchsia/hardware/usb/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>

#include <fbl/auto_lock.h>
#include <usb/usb.h>

#include "src/lib/listnode/listnode.h"

#define CDC_SUPPORTED_VERSION 0x0110 /* 1.10 */

typedef struct {
  uint8_t addr;
  uint16_t max_packet_size;
} ecm_endpoint_t;

typedef struct txn_info {
  ethernet_netbuf_t netbuf;
  ethernet_impl_queue_tx_callback completion_cb;
  void* cookie;
  list_node_t node;
} txn_info_t;

namespace usb_cdc_ecm {
class EcmCtx {
 public:
  explicit EcmCtx() = default;
  // TODO:(fxbug.dev/107717): Simplify ParseUsbDescriptor function of class EcmCtx in
  // usb-cdc-ecm-lib
  zx_status_t ParseUsbDescriptor(usb_desc_iter_t* usb, usb_endpoint_descriptor_t** int_ep,
                                 usb_endpoint_descriptor_t** tx_ep,
                                 usb_endpoint_descriptor_t** rx_ep,
                                 usb_interface_descriptor_t** default_ifc,
                                 usb_interface_descriptor_t** data_ifc);

  void EcmInterruptComplete(void* cookie, usb_request_t* request) {
    sync_completion_signal(&completion);
  }

  list_node_t* tx_txn_bufs() TA_REQ(tx_mutex) { return &tx_txn_bufs_; }
  list_node_t* tx_pending_infos() TA_REQ(tx_mutex) { return &tx_pending_infos_; }

  zx_device_t* zxdev = nullptr;
  zx_device_t* usb_device = nullptr;
  usb_protocol_t usbproto;
  // Ethernet lock -- must be acquired after tx_mutex
  // when both locks are held.
  fbl::Mutex ethernet_mutex;
  ethernet_ifc_protocol_t ethernet_ifc{};

  // Device attributes
  uint8_t mac_addr[ETH_MAC_SIZE] = {
      0,
  };
  uint16_t mtu = 0;

  // Connection attributes
  bool online = false;
  uint32_t ds_bps = 0;
  uint32_t us_bps = 0;

  // Interrupt handling
  ecm_endpoint_t int_endpoint{};
  usb_request_t* int_txn_buf = nullptr;
  sync_completion_t completion{};
  std::optional<thrd_t> int_thread = 0;

  // Send context
  // TX lock -- Must be acquired before ethernet_mutex
  // when both locks are held.
  fbl::Mutex tx_mutex TA_ACQ_BEFORE(ethernet_mutex);
  ecm_endpoint_t tx_endpoint{};
  bool unbound = false;            // set to true when device is going away. Guarded by tx_mutex
  uint64_t tx_endpoint_delay = 0;  // wait time between 2 transmit requests

  size_t parent_req_size = 0;

  // Receive context
  ecm_endpoint_t rx_endpoint{};
  uint64_t rx_endpoint_delay = 0;  // wait time between 2 recv requests
  uint16_t rx_packet_filter = 0;

 private:
  // TODO: move these into a new class and split the ctx

  // Returns true if CDC version is supported
  bool ParseCdcHeader(usb_cs_header_interface_descriptor_t* header_desc);

  // Returns true if its able to successfully parse the interface descriptor passed.
  bool ParseCdcEthernetDescriptor(usb_cs_ethernet_interface_descriptor_t* desc);

  list_node_t tx_txn_bufs_{};       // list of usb_request_t
  list_node_t tx_pending_infos_{};  // list of txn_info_t
};

}  // namespace usb_cdc_ecm

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_LIB_H_
