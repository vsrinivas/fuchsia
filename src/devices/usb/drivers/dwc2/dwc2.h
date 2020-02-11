// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_USB_DWC2_DWC2_H_
#define ZIRCON_SYSTEM_DEV_USB_DWC2_DWC2_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <threads.h>
#include <zircon/hw/usb.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/usb/dci.h>
#include <ddktl/protocol/usb/phy.h>
#include <fbl/mutex.h>
#include <usb/dwc2/metadata.h>
#include <usb/request-cpp.h>

#include "usb_dwc_regs.h"

namespace dwc2 {

class Dwc2;
using Dwc2Type = ddk::Device<Dwc2, ddk::UnbindableDeprecated, ddk::SuspendableNew>;

class Dwc2 : public Dwc2Type, public ddk::UsbDciProtocol<Dwc2, ddk::base_protocol> {
 public:
  explicit Dwc2(zx_device_t* parent) : Dwc2Type(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);
  zx_status_t Init();
  int IrqThread();

  // Device protocol implementation.
  void DdkUnbindDeprecated();
  void DdkRelease();
  void DdkSuspendNew(ddk::SuspendTxn txn);

  // USB DCI protocol implementation.
  void UsbDciRequestQueue(usb_request_t* req, const usb_request_complete_t* cb);
  zx_status_t UsbDciSetInterface(const usb_dci_interface_protocol_t* interface);
  zx_status_t UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                             const usb_ss_ep_comp_descriptor_t* ss_comp_desc);
  zx_status_t UsbDciDisableEp(uint8_t ep_address);
  zx_status_t UsbDciEpSetStall(uint8_t ep_address);
  zx_status_t UsbDciEpClearStall(uint8_t ep_address);
  size_t UsbDciGetRequestSize();
  zx_status_t UsbDciCancelAll(uint8_t ep_address);

 private:
  enum class Ep0State {
    DISCONNECTED,
    IDLE,
    DATA_OUT,
    DATA_IN,
    STATUS_OUT,
    STATUS_IN,
    STALL,
  };

  using Request = usb::BorrowedRequest<void>;
  using RequestQueue = usb::BorrowedRequestQueue<void>;

  struct Endpoint {
    // Requests waiting to be processed.
    RequestQueue queued_reqs __TA_GUARDED(lock);
    // Request currently being processed.
    usb_request_t* current_req __TA_GUARDED(lock) = nullptr;

    // Values for current USB request
    uint32_t req_offset;
    uint32_t req_xfersize;
    uint32_t req_length;
    uint32_t phys;

    // Used for synchronizing endpoint state and ep specific hardware registers.
    // This should be acquired before Dwc2.lock_ if acquiring both locks.
    fbl::Mutex lock;

    uint16_t max_packet_size;
    uint8_t ep_num;
    bool enabled;
    // Endpoint type: control, bulk, interrupt or isochronous
    uint8_t type;
  };

  DISALLOW_COPY_ASSIGN_AND_MOVE(Dwc2);

  void FlushTxFifo(uint32_t fifo_num);
  void FlushRxFifo();
  zx_status_t InitController();
  void SetConnected(bool connected);
  void StartEp0();
  void StartEndpoints();
  void HandleEp0Setup();
  void HandleEp0Status(bool is_in);
  void HandleEp0TransferComplete();
  void HandleTransferComplete(uint8_t ep_num);
  void EnableEp(uint8_t ep_num, bool enable);
  void QueueNextRequest(Endpoint* ep) __TA_REQUIRES(ep->lock);
  void StartTransfer(Endpoint* ep, uint32_t length) __TA_REQUIRES(ep->lock);

  uint32_t ReadTransfered(Endpoint* ep);

  // Interrupt handlers
  void HandleReset();
  void HandleSuspend();
  void HandleEnumDone();
  void HandleInEpInterrupt();
  void HandleOutEpInterrupt();

  zx_status_t HandleSetupRequest(size_t* out_actual);
  void SetAddress(uint8_t address);

  inline ddk::MmioBuffer* get_mmio() { return &*mmio_; }

  Endpoint endpoints_[DWC_MAX_EPS];

  // Used for synchronizing global state
  // and non ep specific hardware registers.
  // Endpoint.lock should be acquired first
  // when acquiring both locks.
  fbl::Mutex lock_;

  zx::bti bti_;
  // DMA buffer for endpoint zero requests
  ddk::IoBuffer ep0_buffer_;
  // Current endpoint zero request
  usb_setup_t cur_setup_ = {};
  Ep0State ep0_state_ = Ep0State::DISCONNECTED;

  ddk::PDev pdev_;
  std::optional<ddk::UsbDciInterfaceProtocolClient> dci_intf_;
  std::optional<ddk::UsbPhyProtocolClient> usb_phy_;

  std::optional<ddk::MmioBuffer> mmio_;

  zx::interrupt irq_;
  thrd_t irq_thread_;

  dwc2_metadata_t metadata_;
  bool connected_ = false;
  bool configured_ = false;
};

}  // namespace dwc2

#endif  // ZIRCON_SYSTEM_DEV_USB_DWC2_DWC2_H_
