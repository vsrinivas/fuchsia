// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_H_

#include <fuchsia/hardware/ethernet/cpp/banjo.h>
#include <fuchsia/hardware/usb/composite/cpp/banjo.h>
#include <fuchsia/hardware/usb/cpp/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/operation/ethernet.h>
#include <lib/zircon-internal/align.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/device/ethernet.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/listnode.h>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>

#include "lib/ddk/binding_priv.h"
#include "usb-cdc-ecm-lib.h"

namespace usb_cdc_ecm {

class UsbCdcEcm;
using DeviceType =
    ddk::Device<UsbCdcEcm, ddk::GetProtocolable, ddk::Initializable, ddk::Unbindable>;

class UsbCdcEcm : public DeviceType,
                  public ddk::EthernetImplProtocol<UsbCdcEcm, ddk::base_protocol> {
 public:
  explicit UsbCdcEcm(zx_device_t* bus_device) : Device(bus_device) {}
  ~UsbCdcEcm() { EcmFree(); }

  void DdkInit(ddk::InitTxn txn);
  static zx_status_t EcmBind(void* ctx, zx_device_t* device);
  void DdkUnbind(ddk::UnbindTxn txn) __TA_EXCLUDES(&ecm_ctx_.tx_mutex);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);

  // ZX_PROTOCOL_ETHERNET_IMPL ops.
  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info);
  void EthernetImplStop() __TA_EXCLUDES(&ecm_ctx_.ethernet_mutex, &ecm_ctx_.tx_mutex);
  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc)
      __TA_EXCLUDES(&ecm_ctx_.ethernet_mutex);
  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie)
      __TA_EXCLUDES(&ecm_ctx_.tx_mutex);
  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const uint8_t* data,
                                   size_t data_size);
  void EthernetImplGetBti(zx::bti* bti) { bti->reset(); }

  // Function invoked by the interrupt thread created by DdkInit.
  // The context of type EcmCtx is passed to it. The thread checks the usb request queue and acts on
  // it. Returns the status of the response of the usb requests queue if its not ZX_OK. An interrupt
  // handler is invoked otherwise.
  static int EcmIntHandlerThread(void*);

  // Interrupt handler function invoked by the interrupt handler thread. It receives the usb_request
  // it has to work on. If the response is less than the size of (usb_cdc_notification_t) the
  // interrupt is ignored.
  static void EcmHandleInterrupt(void*, usb_request_t* request);

 private:
  EcmCtx ecm_ctx_;

  // Returns after the interrupt thread is joined and all the pending usb requests are released.
  void EcmFree();

  // Returns after copying the end point information from usb_endpoint_descriptor_t to
  // ecm_endpoint_t that are passed
  void CopyEndpointInfo(ecm_endpoint_t* ep_info, usb_endpoint_descriptor_t* desc);

  // Returns the status after setting the packet filter based on the mode. If it fails setting the
  // packet filter, it returns corresponding error status.
  zx_status_t SetPacketFilter(uint16_t mode, bool on);

  // Helper function that returns after calling the completion call back of txn_info_t with the
  // status passed
  void CompleteTxn(txn_info_t* txn, zx_status_t status);

  // Returns with ZX_OK if its able to set the completion callback and queues the request
  // successfully. It returns with the appropriate error status otherwise.
  zx_status_t QueueRequest(const uint8_t* data, size_t length, usb_request_t* req);
  zx_status_t SendLocked(ethernet_netbuf_t* netbuf) __TA_REQUIRES(&ecm_ctx_.tx_mutex);

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
  void UsbRecv(usb_request_t* request) __TA_EXCLUDES(&ecm_ctx_.ethernet_mutex);

  // Usb completion routines
  void UsbReadComplete(usb_request_t* request);
  void UsbWriteComplete(usb_request_t* request)
      __TA_EXCLUDES(&ecm_ctx_.ethernet_mutex, &ecm_ctx_.tx_mutex);

  // Returns after updating the online status.
  static void EcmUpdateOnlineStatus(void*, bool is_online) __TA_EXCLUDES(&ecm_ctx_.ethernet_mutex);

  static bool WantInterface(usb_interface_descriptor_t* intf, void* arg) {
    return intf->b_interface_class == USB_CLASS_CDC;
  }
};

}  // namespace usb_cdc_ecm

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_H_
