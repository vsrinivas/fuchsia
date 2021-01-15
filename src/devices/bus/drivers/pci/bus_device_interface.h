
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_BUS_DEVICE_INTERFACE_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_BUS_DEVICE_INTERFACE_H_
#include <lib/zx/bti.h>
#include <lib/zx/msi.h>
#include <stdint.h>
#include <zircon/errors.h>

#include <fbl/ref_ptr.h>

namespace pci {

// Forward declaration to avoid device.h
class Device;
// This interface allows for bridges/devices to communicate with the top level
// Bus object to add and remove themselves from the device list of their
// particular bus instance, obtain their BTIs, and make MSI allocations. This
// becomes more important as multiple bus instances with differing segment
// groups become a reality.
class BusDeviceInterface {
 public:
  virtual ~BusDeviceInterface() = default;
  // Get the BTI at |index| for a device.
  virtual zx_status_t GetBti(const pci::Device* device, uint32_t index, zx::bti* bti) = 0;
  // Allocate |count| messagge signaled interrupts for a device.
  virtual zx_status_t AllocateMsi(uint32_t count, zx::msi* msi) = 0;
  // Request a channel for a sysmem connection.
  virtual zx_status_t ConnectSysmem(zx::channel channel) = 0;
  // Add device to the Bus device tree.
  virtual zx_status_t LinkDevice(fbl::RefPtr<pci::Device> device) = 0;
  // Remove a device from the Bus device tree.
  virtual zx_status_t UnlinkDevice(pci::Device* device) = 0;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_BUS_DEVICE_INTERFACE_H_
