// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

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
private:
    mutable fbl::Mutex dev_list_lock_;
    pci::DeviceList device_list_;
};

} // namespace pci
