// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_H_

#include <fuchsia/hardware/ethernet/cpp/banjo.h>
#include <fuchsia/hardware/usb/c/banjo.h>
#include <fuchsia/hardware/usb/request/c/banjo.h>
#include <lib/operation/ethernet.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/compiler.h>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <src/lib/listnode/listnode.h>
#include <usb/usb.h>

#include "src/connectivity/ethernet/drivers/usb-cdc-ecm/usb-cdc-ecm-lib.h"
#include "usb/request-cpp.h"

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
  void HandleInterrupt(usb::Request<void>& request);

  void InterruptComplete(usb_request_t* request) { sync_completion_signal(&completion_); }

  zx_status_t SetPacketFilterMode(uint16_t mode, bool on);

  // Returns with ZX_OK if its able to set the completion callback and queues the request
  // successfully. It returns with the appropriate error status otherwise.
  zx_status_t SendLocked(const eth::BorrowedOperation<void>& op) __TA_REQUIRES(&mutex_);

  void UsbReadComplete(usb_request_t* request);
  void UsbWriteComplete(usb_request_t* request);

  void UpdateOnlineStatus(bool is_online);

  usb::UsbDevice usb_;

  // Ethernet lock -- must be acquired after tx_mutex_ when both locks are held.
  fbl::Mutex ethernet_mutex_;
  ethernet_ifc_protocol_t ethernet_ifc_ = {};

  // Device attributes
  MacAddress mac_addr_;
  uint16_t mtu_;

  // Connection attributes
  bool online_ __TA_GUARDED(ethernet_mutex_) = false;
  uint32_t ds_bps_ = 0;
  uint32_t us_bps_ = 0;

  // Interrupt handling
  std::optional<EcmEndpoint> int_endpoint_;
  std::optional<usb::Request<void>> interrupt_request_;
  sync_completion_t completion_;
  thrd_t int_thread_;

  // Send context
  // TX lock -- Must be acquired before ethernet_mutex when both locks are held.
  fbl::Mutex mutex_;
  std::optional<EcmEndpoint> tx_endpoint_;
  usb::RequestPool<void> tx_request_pool_ TA_GUARDED(mutex_);
  eth::BorrowedOperationQueue<> pending_tx_queue_ TA_GUARDED(mutex_);
  bool unbound_ __TA_GUARDED(&mutex_) = false;
  uint64_t tx_endpoint_delay_;

  size_t parent_req_size_;

  // Receive context
  std::optional<EcmEndpoint> rx_endpoint_;
  usb::RequestPool<void> rx_request_pool_ TA_GUARDED(mutex_);
  uint64_t rx_endpoint_delay_;  // wait time between 2 recv requests
  uint16_t rx_packet_filter_;
};

}  // namespace usb_cdc_ecm

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_H_
