// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_DEVICE_H_
#define SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_DEVICE_H_

#include <lib/mmio/mmio.h>
#include <zircon/hw/usb.h>
#include <zircon/types.h>

#include <array>
#include <memory>

#include <fbl/array.h>
#include <usb/request-cpp.h>

#include "usb-request-queue.h"

namespace mt_usb_hci {

// The maximum number of endpoints any USB device could theoretically support.  Endpoint addresses
// are 4-bit values.
constexpr int kMaxEndpointCount = 16;

// UsbDevice is a usb spec-compliant device.
class UsbDevice {
 public:
  virtual ~UsbDevice() = default;

  // Return the id (e.g. usb address) for this device.
  virtual uint32_t id() const = 0;

  // Return the id of the usb hub this device is attached to.
  virtual uint32_t hub_id() const = 0;

  // Return the speed of this device.
  virtual const usb_speed_t& speed() const = 0;

  // Process a new usb request.
  virtual zx_status_t HandleRequest(usb::BorrowedRequest<> req) = 0;

  // Enable processing for the as-described endpoint on this device.
  virtual zx_status_t EnableEndpoint(const usb_endpoint_descriptor_t& desc) = 0;

  // Disable processing for the as-described endpoint on this device.
  virtual zx_status_t DisableEndpoint(const usb_endpoint_descriptor_t& desc) = 0;

  // Return the maximum packet transfer size (i.e. w_max_packet_size) for the given endpoint.
  virtual size_t GetMaxTransferSize(uint8_t ep) = 0;
};

// A HardwareDevice is a UsbDevice corresponding to physical (i.e. non-emulated) hardware.
class HardwareDevice : public UsbDevice {
 public:
  HardwareDevice(uint32_t id, uint32_t hub_id, usb_speed_t speed, ddk::MmioView usb)
      : usb_(usb), id_(id), hub_id_(hub_id), speed_(speed) {
    ep_q_[0] = std::make_unique<ControlQueue>(usb);
  }

  ~HardwareDevice() = default;

  // Assign, copy, and move disallowed.
  HardwareDevice(HardwareDevice&&) = delete;
  HardwareDevice& operator=(HardwareDevice&&) = delete;

  uint32_t id() const override { return id_; }
  uint32_t hub_id() const override { return hub_id_; }
  const usb_speed_t& speed() const override { return speed_; }

  std::unique_ptr<RequestQueue>& ep_queue(uint8_t ep) {
    ZX_ASSERT_MSG(ep_q_.at(ep), "endpoint not configured");
    return ep_q_[ep];
  }

  zx_status_t HandleRequest(usb::BorrowedRequest<> req) override;
  // Endpoint processing for a HardwareDevice involves starting the requisite RequestQueue.
  zx_status_t EnableEndpoint(const usb_endpoint_descriptor_t& desc) override;
  zx_status_t DisableEndpoint(const usb_endpoint_descriptor_t& desc) override;
  size_t GetMaxTransferSize(uint8_t ep) override;

  // Perform USB device enumeration.  If this routine succeeds, the device will be in the
  // configured state.
  zx_status_t Enumerate();

  // This device was disconnected from the bus.  All endpoint handlers will be halted.
  void Disconnect();

  // Cancel all pending endpoint requests.
  zx_status_t CancelAll(uint8_t ep);

 private:
  // Resize the endpoint FIFO to hold the given packet size.
  void ResizeFifo(uint8_t ep, size_t pkt_sz);

  // The USB register mmio.
  ddk::MmioView usb_;

  // The id of this device.
  const uint32_t id_;

  // Device id of the hub this device is attached to.
  const uint32_t hub_id_;

  // The speed of this device.
  const usb_speed_t speed_;

  // Array of RequestQueue unique_ptrs indexed by endpoint-number.
  std::array<std::unique_ptr<RequestQueue>, kMaxEndpointCount> ep_q_;
};

}  // namespace mt_usb_hci

#endif  // SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_DEVICE_H_
