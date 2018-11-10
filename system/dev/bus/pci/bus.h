// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_BUS_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_BUS_H_

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pciroot.h>

namespace pci {

class Bus;
using PciBusType = ddk::Device<Bus>;

class Bus : public PciBusType,
            public ddk::PcirootProtocolProxy {
public:
    static zx_status_t Create(zx_device_t* parent);
    zx_status_t Init(void);
    void DdkRelease();

private:
    // Our constructor exists to fulfill the mixin constructors
    explicit Bus(zx_device_t* parent, const pciroot_protocol_t* proto)
        : PciBusType(parent), PcirootProtocolProxy(proto) {}
};

} // namespace pci

#endif // ZIRCON_SYSTEM_DEV_BUS_PCI_BUS_H_
