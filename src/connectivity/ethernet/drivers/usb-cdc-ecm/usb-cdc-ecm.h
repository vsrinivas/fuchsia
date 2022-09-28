// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_H_

#include <fuchsia/hardware/ethernet/cpp/banjo.h>
#include <fuchsia/hardware/usb/c/banjo.h>
#include <fuchsia/hardware/usb/request/c/banjo.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb.h>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <src/lib/listnode/listnode.h>
#include <usb/usb.h>

#include "src/connectivity/ethernet/drivers/usb-cdc-ecm/usb-cdc-ecm-lib.h"

namespace usb_cdc_ecm {

class UsbCdcEcm;

using UsbCdcEcmType = ::ddk::Device<UsbCdcEcm, ddk::Initializable, ddk::Unbindable>;

class UsbCdcEcm : public UsbCdcEcmType,
                  public ddk::EthernetImplProtocol<UsbCdcEcm, ddk::base_protocol> {
 public:
  explicit UsbCdcEcm(zx_device_t* parent, const usb::UsbDevice& usb)
      : UsbCdcEcmType(parent), usb_(usb) {}
  ~UsbCdcEcm();

  void DdkInit(ddk::InitTxn txn);
  zx_status_t Init();
  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // ZX_PROTOCOL_ETHERNET_IMPL ops.
  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info);
  void EthernetImplStop();
  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc);
  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const uint8_t* data,
                                   size_t data_size);
  void EthernetImplGetBti(zx::bti* bti) { bti->reset(); }

 private:
  // Function invoked by the interrupt thread created by DdkInit.
  // The context of type EcmCtx is passed to it. The thread checks the usb request queue and acts on
  // it. Returns the status of the response of the usb requests queue if its not ZX_OK. An interrupt
  // handler is invoked otherwise.
  int InterruptThread();

  // Interrupt handler function invoked by the interrupt handler thread. It receives the usb_request
  // it has to work on. If the response is less than the size of (usb_cdc_notification_t) the
  // interrupt is ignored.
  void HandleInterrupt(usb_request_t* request);

  void InterruptComplete(usb_request_t* request) { sync_completion_signal(&completion_); }

  void Free();

  zx_status_t SetPacketFilterMode(uint16_t mode, bool on);

  // Returns with ZX_OK if its able to set the completion callback and queues the request
  // successfully. It returns with the appropriate error status otherwise.
  zx_status_t QueueRequest(const uint8_t* data, size_t length, usb_request_t* req);
  zx_status_t SendLocked(ethernet_netbuf_t* netbuf) __TA_REQUIRES(&tx_mutex_);

  // Write completion callback. Normally -- this will simply acquire the TX lock, release it,
  // and re-queue the USB request.
  // The error case is a bit more complicated. We set the reset bit on the request, and queue
  // a packet that triggers a reset (asynchronously). We then immediately return to the interrupt
  // thread with the lock held to allow for interrupt processing to take place. Once the reset
  // completes, this function is called again with the lock still held, and request processing
  // continues normally. It is necessary to keep the lock held after returning in the error case
  // because we do not want other packets to get queued out-of-order while the asynchronous
  // operation is in progress. Note: the assumption made here is that no rx transmissions will be
  // processed in parallel, so we do not maintain an rx mutex.
  void UsbReceive(usb_request_t* request);

  void UsbReadComplete(usb_request_t* request);
  void UsbWriteComplete(usb_request_t* request) __TA_NO_THREAD_SAFETY_ANALYSIS;

  void UpdateOnlineStatus(bool is_online);

  usb::UsbDevice usb_;

  // Ethernet lock -- must be acquired after tx_mutex_ when both locks are held.
  fbl::Mutex ethernet_mutex_;
  ethernet_ifc_protocol_t ethernet_ifc_ = {};

  // Device attributes
  MacAddress mac_addr_;
  uint16_t mtu_;

  // Connection attributes
  bool online_ = false;
  uint32_t ds_bps_ = 0;
  uint32_t us_bps_ = 0;

  // Interrupt handling
  std::optional<EcmEndpoint> int_endpoint_;
  usb_request_t* int_txn_buf_;
  sync_completion_t completion_;
  thrd_t int_thread_;

  // Send context
  // TX lock -- Must be acquired before ethernet_mutex when both locks are held.
  fbl::Mutex tx_mutex_;
  std::optional<EcmEndpoint> tx_endpoint_;
  list_node_t tx_txn_bufs_ TA_GUARDED(tx_mutex_);       // list of usb_request_t
  list_node_t tx_pending_infos_ TA_GUARDED(tx_mutex_);  // list of txn_info_t
  bool unbound_ = false;        // set to true when device is going away. Guarded by tx_mutex_
  uint64_t tx_endpoint_delay_;  // wait time between 2 transmit requests

  size_t parent_req_size_;

  // Receive context
  std::optional<EcmEndpoint> rx_endpoint_;
  uint64_t rx_endpoint_delay_;  // wait time between 2 recv requests
  uint16_t rx_packet_filter_;
};

}  // namespace usb_cdc_ecm

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_H_
