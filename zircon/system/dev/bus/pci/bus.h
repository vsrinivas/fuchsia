// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "bridge.h"
#include "config.h"
#include "device.h"
#include "root.h"
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pciroot.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

namespace pci {

class Bus;
using PciBusType = ddk::Device<Bus>;

class Bus : public PciBusType {
public:
    using BridgeList = fbl::Vector<fbl::RefPtr<Bridge>>;
    using AllDevicesList = fbl::WAVLTree<pci_bdf_t,
                                         fbl::RefPtr<pci::Device>,
                                         pci::Device::KeyTraitsSortByBdf,
                                         pci::Device::BusListTraits>;
    static zx_status_t Create(zx_device_t* parent);
    static void LinkDeviceToBus(fbl::RefPtr<pci::Device> device) {
        device_list_.insert(device);
    }

    static void UnlinkDeviceFromBus(pci::Device* device) {
        device_list_.erase(*device);
    }

    void DdkRelease();

private:
    // Our constructor exists to fulfill the mixin constructors
    Bus(zx_device_t* parent, const pciroot_protocol_t* proto)
        : PciBusType(parent), // fulfills the DDK mixins
          pciroot_(proto) {}

    // Utility methods for the bus driver
    zx_status_t Initialize(void);
    // Map an ecam VMO for Bus and Config use.
    zx_status_t MapEcam(void);
    // Scan all buses downstream from the root within the start and end
    // bus values given to the Bus driver through Pciroot.
    zx_status_t ScanDownstream(void);
    ddk::PcirootProtocolClient& pciroot(void) { return pciroot_; }
    // Scan a specific bus
    void ScanBus(UpstreamNode* upstream, BridgeList* upstream_list);
    // Creates a Config object for accessing the config space of the device at |bdf|.
    zx_status_t MakeConfig(pci_bdf_t bdf, fbl::RefPtr<Config>* config);

    // members
    ddk::PcirootProtocolClient pciroot_;

    pci_platform_info_t info_;
    mmio_buffer_t ecam_;
    bool has_ecam_;
    fbl::unique_ptr<PciRoot> root_;

    // A global list of all devices in the system is shared across PCI bus instances.
    // Devices are keyed by BDF so they should not experience any collisions.
    // TODO(cja): what about segment group?
    BridgeList bridge_list_;
    static AllDevicesList device_list_;
};

} // namespace pci
