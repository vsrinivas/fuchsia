// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_XHCI_DEVICE_STATE_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_XHCI_DEVICE_STATE_H_

#include <optional>

#include "xhci-hub.h"
#include "xhci-transfer-ring.h"
#include "zircon/compiler.h"

namespace usb_xhci {

// The maximum number of endpoints a USB device can support
constexpr size_t kMaxEndpoints = 32;

class DeviceState {
 public:
  void Disconnect() __TA_REQUIRES(transaction_lock_) { disconnecting_ = true; }

  void reset() __TA_REQUIRES(transaction_lock_) {
    disconnecting_ = true;
    input_context_.reset();
    device_context_.reset();
    slot_ = 0;
    hub_.reset();
    tr_.Deinit();
    for (size_t i = 0; i < kMaxEndpoints; i++) {
      rings_[i].DeinitIfActive();
    }
  }

  void SetDeviceInformation(uint8_t slot, uint8_t port, const std::optional<HubInfo>& hub)
      __TA_REQUIRES(transaction_lock_) {
    slot_ = slot;
    port_ = port;
    hub_ = hub;
    disconnecting_ = false;
  }

  // True if the device state has been initialized, false otherwise.
  bool valid() { return slot_; }

  uint8_t GetPort() { return port_; }

  uint8_t GetSlot() { return slot_; }

  std::optional<HubInfo>& GetHubLocked() __TA_REQUIRES(transaction_lock_) { return hub_; }

  std::optional<HubInfo>& GetHub() {
    fbl::AutoLock l(&transaction_lock_);
    return hub_;
  }

  bool IsDisconnecting() __TA_REQUIRES(transaction_lock_) { return disconnecting_; }

  TransferRing& GetTransferRing() __TA_REQUIRES(transaction_lock_) { return tr_; }

  TransferRing& GetTransferRing(size_t endpoint) __TA_REQUIRES(transaction_lock_) {
    return rings_[endpoint];
  }

  std::unique_ptr<dma_buffer::PagedBuffer>& GetInputContext() __TA_REQUIRES(transaction_lock_) {
    return input_context_;
  }

  std::unique_ptr<dma_buffer::PagedBuffer>& GetDeviceContext() __TA_REQUIRES(transaction_lock_) {
    return device_context_;
  }

  TRBPromise AddressDeviceCommand(UsbXhci* hci, uint8_t slot_id, uint8_t port_id,
                                  std::optional<HubInfo> hub_info, uint64_t* dcbaa,
                                  EventRing* event_ring, CommandRing* command_ring,
                                  ddk::MmioBuffer* mmio, bool bsr);

  zx_status_t InitializeSlotBuffer(const UsbXhci& hci, uint8_t slot_id, uint8_t port_id,
                                   const std::optional<HubInfo>& hub_info,
                                   std::unique_ptr<dma_buffer::PagedBuffer>* out);

  zx_status_t InitializeEndpointContext(const UsbXhci& hci, uint8_t slot_id, uint8_t port_id,
                                        const std::optional<HubInfo>& hub_info,
                                        dma_buffer::PagedBuffer* slot_context_buffer)
      __TA_REQUIRES(transaction_lock_);
  zx_status_t InitializeOutputContextBuffer(const UsbXhci& hci, uint8_t slot_id, uint8_t port_id,
                                            const std::optional<HubInfo>& hub_info, uint64_t* dcbaa,
                                            std::unique_ptr<dma_buffer::PagedBuffer>* out)
      __TA_REQUIRES(transaction_lock_);

  fbl::Mutex& transaction_lock() __TA_RETURN_CAPABILITY(transaction_lock_) {
    return transaction_lock_;
  }

 private:
  uint8_t slot_ = 0;
  uint8_t port_ = 0;
  fbl::Mutex transaction_lock_;
  std::optional<HubInfo> hub_ __TA_GUARDED(transaction_lock_);
  bool disconnecting_ __TA_GUARDED(transaction_lock_) = false;
  TransferRing tr_ __TA_GUARDED(transaction_lock_);
  TransferRing rings_[kMaxEndpoints] __TA_GUARDED(transaction_lock_);
  std::unique_ptr<dma_buffer::PagedBuffer> input_context_ __TA_GUARDED(transaction_lock_);
  std::unique_ptr<dma_buffer::PagedBuffer> device_context_ __TA_GUARDED(transaction_lock_);
};
}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_XHCI_DEVICE_STATE_H_
