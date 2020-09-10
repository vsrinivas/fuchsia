// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_MT_MUSB_PERIPHERAL_MT_USB_H_
#define SRC_DEVICES_USB_DRIVERS_MT_MUSB_PERIPHERAL_MT_USB_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/hw/usb.h>

#include <ddktl/device.h>
#include <ddktl/protocol/usb/dci.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>

namespace mt_usb {

class MtUsb;
using MtUsbType = ddk::Device<MtUsb, ddk::Unbindable>;

class MtUsb : public MtUsbType, public ddk::UsbDciProtocol<MtUsb, ddk::base_protocol> {
 public:
  explicit MtUsb(zx_device_t* parent, pdev_protocol_t* pdev) : MtUsbType(parent), pdev_(pdev) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

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
  DISALLOW_COPY_ASSIGN_AND_MOVE(MtUsb);

  enum Ep0State {
    // Waiting for next setup request.
    EP0_IDLE,
    // Reading data for setup request.
    EP0_READ,
    // Writing data for setup request.
    EP0_WRITE,
  };

  enum EpDirection {
    EP_OUT,
    EP_IN,
  };

  using Request = usb::BorrowedRequest<void>;
  using RequestQueue = usb::BorrowedRequestQueue<void>;

  // This represents a non-control USB endpoint.
  // Endpoint zero is handled separately.
  struct Endpoint {
    // Endpoint number to use when indexing into hardware registers.
    uint8_t ep_num;
    // EP_OUT or EP_IN.
    EpDirection direction;
    // Address from the endpoint descriptor.
    uint8_t address;

    bool enabled __TA_GUARDED(lock) = false;
    uint16_t max_packet_size;

    // Requests waiting to be processed.
    RequestQueue queued_reqs __TA_GUARDED(lock);
    // request currently being processed.
    usb_request_t* current_req __TA_GUARDED(lock) = nullptr;
    RequestQueue complete_reqs __TA_GUARDED(lock);

    // Offset into current_req during read and write.
    size_t cur_offset;

    fbl::Mutex lock;
  };

  zx_status_t Init();
  void InitPhy();
  int IrqThread();

  void HandleSuspend();
  void HandleReset();
  zx_status_t HandleEp0();
  void HandleEndpointTxLocked(Endpoint* ep) __TA_REQUIRES(ep->lock);
  void HandleEndpointRxLocked(Endpoint* ep) __TA_REQUIRES(ep->lock);

  void FifoRead(uint8_t ep_index, void* buf, size_t buflen, size_t* actual);
  void FifoWrite(uint8_t ep_index, const void* buf, size_t length);
  void EpQueueNextLocked(Endpoint* ep) __TA_REQUIRES(ep->lock);
  void StartEndpoint(Endpoint* ep);
  void StartEndpoints();
  void SetStall(Endpoint* ep, bool stall);

  Endpoint* EndpointFromAddress(uint8_t addr);

  inline ddk::MmioBuffer* usb_mmio() { return &*usb_mmio_; }
  inline ddk::MmioBuffer* phy_mmio() { return &*phy_mmio_; }

  ddk::PDev pdev_;
  std::optional<ddk::UsbDciInterfaceProtocolClient> dci_intf_;

  std::optional<ddk::MmioBuffer> usb_mmio_;
  std::optional<ddk::MmioBuffer> phy_mmio_;

  zx::interrupt irq_;
  thrd_t irq_thread_;

  // Number of endpoints we support, not counting ep0.
  static constexpr size_t NUM_EPS = 15;

  Endpoint out_eps_[NUM_EPS];
  Endpoint in_eps_[NUM_EPS];

  // Address assigned to us by the host.
  uint8_t address_ = 0;
  bool set_address_ = false;

  // Current USB configuration. TODO this needs a lock.
  uint8_t configuration_ = 0;

  Ep0State ep0_state_ = EP0_IDLE;
  usb_setup_t cur_setup_;

  uint8_t ep0_data_[UINT16_MAX];
  // Current read/write location in ep0_buffer_
  size_t ep0_data_offset_ = 0;
  // Total length to read or write
  size_t ep0_data_length_ = 0;

  uint8_t ep0_max_packet_;
};

}  // namespace mt_usb

#endif  // SRC_DEVICES_USB_DRIVERS_MT_MUSB_PERIPHERAL_MT_USB_H_
