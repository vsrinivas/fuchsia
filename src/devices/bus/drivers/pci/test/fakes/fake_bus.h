// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_BUS_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_BUS_H_

#include <zircon/hw/pci.h>

#include <ddk/mmio-buffer.h>
#include <ddktl/protocol/pciroot.h>
#include <hwreg/bitfields.h>

#include "../../bus.h"

namespace pci {

class FakeBus : public BusLinkInterface {
 public:
  void LinkDevice(fbl::RefPtr<pci::Device> device) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    devices_.insert(device);
  }

  void UnlinkDevice(pci::Device* device) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    devices_.erase(*device);
  }

  pci::Device& get_device(pci_bdf_t bdf) { return *devices_.find(bdf); }

  // For use with Devices that need to link to a Bus.
  BusLinkInterface* bli() { return static_cast<BusLinkInterface*>(this); }

  const pci::DeviceTree& devices() { return devices_; }

 private:
  fbl::Mutex devices_lock_;
  pci::DeviceTree devices_;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_BUS_H_
