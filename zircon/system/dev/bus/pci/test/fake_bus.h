// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_TEST_FAKE_BUS_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_TEST_FAKE_BUS_H_

#include "../bus.h"
#include <ddk/mmio-buffer.h>
#include <ddktl/protocol/pciroot.h>
#include <hwreg/bitfields.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <zircon/hw/pci.h>

namespace pci {

class FakeBus : public BusLinkInterface {
public:
    void LinkDevice(fbl::RefPtr<pci::Device> device) final {
        fbl::AutoLock dev_list_lock(&dev_list_lock_);
        device_list_.insert(device);
    }

    void UnlinkDevice(pci::Device* device) final {
        fbl::AutoLock dev_list_lock(&dev_list_lock_);
        device_list_.erase(*device);
    }

    pci::Device& get_device(pci_bdf_t bdf) {
        return *device_list_.find(bdf);
    }

    const pci::DeviceList& device_list() { return device_list_; }

private:
    mutable fbl::Mutex dev_list_lock_;
    pci::DeviceList device_list_;
};

} // namespace pci

#endif // ZIRCON_SYSTEM_DEV_BUS_PCI_TEST_FAKE_BUS_H_
