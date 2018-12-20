// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_BUS_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_BUS_H_
#include "config.h"

#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pciroot.h>

namespace pci {

class Bus;
using PciBusType = ddk::Device<Bus>;

class Bus : public PciBusType {
public:
    static zx_status_t Create(zx_device_t* parent);
    void DdkRelease();

private:
    // Utility methods for the bus driver
    zx_status_t Initialize(void);
    // Creates a Config object for accessing the config space for
    // the device at |bdf|.
    zx_status_t MakeConfig(pci_bdf_t bdf, fbl::RefPtr<Config>* config);
    zx_status_t MapEcam(void);
    zx_status_t ScanDownstream(void);
    ddk::PcirootProtocolClient& pciroot(void) { return pciroot_; }
    // Our constructor exists to fulfill the mixin constructors
    Bus(zx_device_t* parent, const pciroot_protocol_t* proto)
        : PciBusType(parent), pciroot_(proto) {}
    
    // members
    ddk::PcirootProtocolClient pciroot_;
    pci_platform_info_t info_;
    mmio_buffer_t ecam_;
    bool has_ecam_;
};

} // namespace pci

#endif // ZIRCON_SYSTEM_DEV_BUS_PCI_BUS_H_
